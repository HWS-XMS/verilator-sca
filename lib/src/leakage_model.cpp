#include "sca/leakage_model.h"

#include <cstring>
#include <stdexcept>

namespace sca {

static int popcount32(uint32_t v) {
    return __builtin_popcount(v);
}

double HammingDistance::compute(const uint32_t* prev, const uint32_t* curr,
                                size_t n_words, size_t /*n_bits*/) const {
    int count = 0;
    for (size_t i = 0; i < n_words; ++i)
        count += popcount32(curr[i] ^ prev[i]);
    return static_cast<double>(count);
}

double HammingWeight::compute(const uint32_t* /*prev*/, const uint32_t* curr,
                               size_t n_words, size_t /*n_bits*/) const {
    int count = 0;
    for (size_t i = 0; i < n_words; ++i)
        count += popcount32(curr[i]);
    return static_cast<double>(count);
}

double Identity::compute(const uint32_t* /*prev*/, const uint32_t* curr,
                          size_t n_words, size_t n_bits) const {
    // Reconstruct the integer value (little-word-endian, like Verilator aval).
    // For signals > 64 bits this will lose precision, but that's inherent to double.
    double val = 0.0;
    double base = 1.0;
    for (size_t i = 0; i < n_words; ++i) {
        uint32_t word = curr[i];
        // Mask the top word to actual bit width.
        if (i == n_words - 1) {
            size_t top_bits = n_bits % 32;
            if (top_bits != 0)
                word &= (1u << top_bits) - 1;
        }
        val += static_cast<double>(word) * base;
        base *= 4294967296.0; // 2^32
    }
    return val;
}

std::unique_ptr<LeakageModel> make_leakage_model(const std::string& name) {
    if (name == "hamming_distance") return std::make_unique<HammingDistance>();
    if (name == "hamming_weight")   return std::make_unique<HammingWeight>();
    if (name == "identity")         return std::make_unique<Identity>();
    throw std::runtime_error("Unknown leakage model: " + name);
}

} // namespace sca
