#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace sca {

class LeakageModel {
public:
    virtual ~LeakageModel() = default;

    /// Compute leakage from previous and current signal values.
    /// @param prev  previous aval words
    /// @param curr  current aval words
    /// @param n_words number of 32-bit words
    /// @param n_bits  total signal width in bits
    virtual double compute(const uint32_t* prev, const uint32_t* curr,
                           size_t n_words, size_t n_bits) const = 0;
};

class HammingDistance : public LeakageModel {
public:
    double compute(const uint32_t* prev, const uint32_t* curr,
                   size_t n_words, size_t n_bits) const override;
};

class HammingWeight : public LeakageModel {
public:
    double compute(const uint32_t* prev, const uint32_t* curr,
                   size_t n_words, size_t n_bits) const override;
};

class Identity : public LeakageModel {
public:
    double compute(const uint32_t* prev, const uint32_t* curr,
                   size_t n_words, size_t n_bits) const override;
};

std::unique_ptr<LeakageModel> make_leakage_model(const std::string& name);

} // namespace sca
