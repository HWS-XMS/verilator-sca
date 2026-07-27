#include "sca/sca.h"

#include "sca/config.h"
#include "sca/hdf5_writer.h"
#include "sca/leakage_model.h"
#include "sca/signal_reader.h"
#include "sca/trace_buffer.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <map>
#include <memory>
#include <vector>

// ---------- static context (single-threaded Verilator) ----------

static sca::Config                              s_cfg;
static std::vector<std::unique_ptr<sca::LeakageModel>> s_models;
static std::vector<sca::SignalReader>            s_readers;
static std::vector<std::string>                 s_signal_names;
static std::vector<std::unique_ptr<sca::TraceBuffer>>  s_bufs;
static std::vector<std::vector<uint32_t>>       s_prev;  // previous values per signal
static std::vector<double>                      s_weights;
static bool                                     s_capturing = false;

// Per-trace metadata (set before/during capture, consumed by end_capture).
static std::map<std::string, std::string>       s_metadata;

// Accumulated traces across multiple captures, one vector per model.
struct TraceRecord {
    std::vector<float> samples;   // flattened: length S or S*C
    std::string timestamp;        // ISO-8601
    std::map<std::string, std::string> metadata;
};
static std::vector<std::vector<TraceRecord>> s_traces; // [model_idx][trace_idx]

// ----------------------------------------------------------------

static std::string iso8601_now() {
    auto now = std::chrono::system_clock::now();
    auto tt  = std::chrono::system_clock::to_time_t(now);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", std::gmtime(&tt));
    return buf;
}

extern "C" {

void sca_init(const char* config_path) {
    s_cfg = sca::parse_config(config_path);

    s_models.clear();
    s_bufs.clear();
    s_readers.clear();
    s_signal_names.clear();
    s_weights.clear();
    s_traces.clear();
    s_metadata.clear();

    for (const auto& name : s_cfg.leakage_models)
        s_models.push_back(sca::make_leakage_model(name));

    if (s_cfg.auto_discover) {
        auto discovered = sca::discover_all_signals_named(s_cfg.scope);
        s_readers = std::move(discovered.readers);
        s_signal_names = std::move(discovered.names);
        s_weights.assign(s_readers.size(), 1.0);
    } else {
        for (const auto& sc : s_cfg.signals) {
            s_readers.emplace_back(sc.path);
            s_weights.push_back(sc.weight);
        }
    }

    for (size_t m = 0; m < s_models.size(); ++m)
        s_bufs.push_back(std::make_unique<sca::TraceBuffer>(s_readers.size()));
    s_traces.resize(s_models.size());

    // Initialize previous-value buffers to zero.
    s_prev.resize(s_readers.size());
    for (size_t i = 0; i < s_readers.size(); ++i)
        s_prev[i].assign(s_readers[i].n_words(), 0);

    std::string model_names;
    for (size_t m = 0; m < s_cfg.leakage_models.size(); ++m) {
        if (m > 0) model_names += ", ";
        model_names += s_cfg.leakage_models[m];
    }
    std::printf("[sca] initialized: %zu signal(s), model(s)=%s\n",
                s_readers.size(), model_names.c_str());
}

void sca_set_metadata(const char* key, const char* value) {
    s_metadata[key] = value;
}

void sca_start_capture() {
    for (auto& buf : s_bufs) buf->clear();

    // Snapshot current values as "previous" so the first sample has a valid diff.
    for (size_t i = 0; i < s_readers.size(); ++i) {
        const uint32_t* cur = s_readers[i].read();
        std::memcpy(s_prev[i].data(), cur, s_readers[i].n_words() * sizeof(uint32_t));
    }
    s_capturing = true;
}

void sca_sample() {
    if (!s_capturing) return;

    // Read each signal once, then apply all models.
    std::vector<const uint32_t*> cur_ptrs(s_readers.size());
    for (size_t i = 0; i < s_readers.size(); ++i)
        cur_ptrs[i] = s_readers[i].read();

    std::vector<double> per_signal(s_readers.size());
    for (size_t m = 0; m < s_models.size(); ++m) {
        for (size_t i = 0; i < s_readers.size(); ++i)
            per_signal[i] = s_models[m]->compute(
                s_prev[i].data(), cur_ptrs[i],
                s_readers[i].n_words(), s_readers[i].n_bits());
        s_bufs[m]->append_sample(per_signal, s_weights);
    }

    // Update previous values after all models have been applied.
    for (size_t i = 0; i < s_readers.size(); ++i)
        std::memcpy(s_prev[i].data(), cur_ptrs[i], s_readers[i].n_words() * sizeof(uint32_t));
}

void sca_end_capture() {
    s_capturing = false;

    std::string ts = iso8601_now();

    for (size_t m = 0; m < s_models.size(); ++m) {
        TraceRecord rec;
        rec.timestamp = ts;
        rec.metadata  = s_metadata;

        if (s_cfg.save_per_signal) {
            size_t ns = s_bufs[m]->n_samples();
            size_t nc = s_bufs[m]->n_signals();
            rec.samples.resize(ns * nc);
            const auto& ps = s_bufs[m]->per_signal();
            for (size_t s = 0; s < ns; ++s)
                for (size_t c = 0; c < nc; ++c)
                    rec.samples[s * nc + c] = static_cast<float>(ps[c][s]);
        } else {
            const auto& agg = s_bufs[m]->aggregated();
            rec.samples.resize(agg.size());
            for (size_t i = 0; i < agg.size(); ++i)
                rec.samples[i] = static_cast<float>(agg[i]);
        }

        s_traces[m].push_back(std::move(rec));
    }
}

void sca_save(const char* output_path) {
    bool any = false;
    for (const auto& mt : s_traces)
        if (!mt.empty()) { any = true; break; }
    if (!any) {
        std::printf("[sca] no traces to save\n");
        return;
    }

    // Remove stale file so the first writer creates fresh, subsequent writers append.
    std::remove(output_path);

    bool multi = s_models.size() > 1;

    for (size_t m = 0; m < s_models.size(); ++m) {
        if (s_traces[m].empty()) continue;

        std::string series_name = s_cfg.series;
        if (multi)
            series_name += "_" + s_cfg.leakage_models[m];

        size_t sample_len = s_traces[m][0].samples.size();
        std::vector<size_t> sample_shape;

        if (s_cfg.save_per_signal) {
            size_t ns = sample_len / s_bufs[m]->n_signals();
            sample_shape = {ns, s_bufs[m]->n_signals()};
        } else {
            sample_shape = {sample_len};
        }

        sca::Hdf5Writer writer(output_path, s_cfg.experiment, series_name,
                               sample_len, sample_shape);

        if (s_cfg.save_per_signal && !s_signal_names.empty())
            writer.write_string_dataset("signal_names", s_signal_names);

        for (const auto& rec : s_traces[m])
            writer.write_trace(rec.samples.data(), rec.timestamp, rec.metadata);

        writer.close();

        std::printf("[sca] saved %zu trace(s) (%zu samples each) to %s [%s]\n",
                    s_traces[m].size(), sample_len, output_path,
                    s_cfg.leakage_models[m].c_str());
    }
}

} // extern "C"
