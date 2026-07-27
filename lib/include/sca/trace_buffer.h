#pragma once

#include <cstddef>
#include <vector>

namespace sca {

class TraceBuffer {
public:
    explicit TraceBuffer(size_t n_signals);

    void clear();
    void append_sample(const std::vector<double>& per_signal,
                       const std::vector<double>& weights);

    size_t n_samples() const { return aggregated_.size(); }
    size_t n_signals() const { return n_signals_; }

    const std::vector<double>& aggregated() const { return aggregated_; }
    /// Returns per-signal traces; outer index = signal, inner = sample.
    const std::vector<std::vector<double>>& per_signal() const { return per_signal_; }

private:
    size_t n_signals_;
    std::vector<double> aggregated_;
    std::vector<std::vector<double>> per_signal_;
};

} // namespace sca
