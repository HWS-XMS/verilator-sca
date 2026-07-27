#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sca {

struct DiscoveredSignals;

class SignalReader {
public:
    /// Resolve a signal by hierarchical path via Verilator's internal tables.
    explicit SignalReader(const std::string& path);

    /// Read current value into internal buffer and return pointer to aval words.
    const uint32_t* read();

    size_t n_words() const { return n_words_; }
    size_t n_bits() const { return n_bits_; }

private:
    void*  datap_;
    size_t n_bits_;
    size_t n_words_;
    std::vector<uint32_t> aval_buf_;

    /// Construct directly from a resolved pointer (used by discover).
    SignalReader(void* datap, size_t n_bits);

    friend std::vector<SignalReader>
    discover_all_signals(const std::string& root_scope);
    friend DiscoveredSignals
    discover_all_signals_named(const std::string& root_scope);
};

struct DiscoveredSignals {
    std::vector<SignalReader> readers;
    std::vector<std::string> names;
};

/// Discover all signals under root_scope via Verilator direct memory access.
std::vector<SignalReader> discover_all_signals(const std::string& root_scope);

/// Discover all signals, returning both readers and hierarchical names.
DiscoveredSignals discover_all_signals_named(const std::string& root_scope);

} // namespace sca
