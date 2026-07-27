#include "sca/trace_buffer.h"

namespace sca {

TraceBuffer::TraceBuffer(size_t n_signals)
    : n_signals_(n_signals), per_signal_(n_signals) {}

void TraceBuffer::clear() {
    aggregated_.clear();
    for (auto& v : per_signal_) v.clear();
}

void TraceBuffer::append_sample(const std::vector<double>& per_signal,
                                const std::vector<double>& weights) {
    double agg = 0.0;
    for (size_t i = 0; i < n_signals_; ++i) {
        per_signal_[i].push_back(per_signal[i]);
        agg += weights[i] * per_signal[i];
    }
    aggregated_.push_back(agg);
}

} // namespace sca
