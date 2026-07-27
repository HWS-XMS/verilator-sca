#include "sca/signal_reader.h"

#include <cstring>
#include <stdexcept>

#include "verilated.h"
#include "verilated_syms.h"

namespace sca {

// --- private: construct from already-resolved pointer ---

SignalReader::SignalReader(void* datap, size_t n_bits)
    : datap_(datap)
    , n_bits_(n_bits)
    , n_words_((n_bits + 31) / 32)
    , aval_buf_(n_words_, 0) {}

// --- public: resolve a single signal by hierarchical path ---

static const VerilatedScopeNameMap* get_scope_map() {
    const VerilatedScopeNameMap* scopes =
        Verilated::threadContextp()->scopeNameMap();
    if (!scopes)
        throw std::runtime_error("SignalReader: scopeNameMap is null");
    return scopes;
}

SignalReader::SignalReader(const std::string& path) {
    // Split "A.B.C.var" into scope "A.B.C" and var name "var".
    auto pos = path.rfind('.');
    if (pos == std::string::npos)
        throw std::runtime_error(
            "SignalReader: invalid path (no '.') " + path);

    std::string scope_name = path.substr(0, pos);
    std::string var_name   = path.substr(pos + 1);

    const VerilatedScopeNameMap* scopes = get_scope_map();

    // Find the scope.
    const VerilatedScope* scope = nullptr;
    for (const auto& [name, s] : *scopes) {
        if (scope_name == name) { scope = s; break; }
    }
    if (!scope)
        throw std::runtime_error(
            "SignalReader: cannot resolve scope " + scope_name);

    const VerilatedVarNameMap* vars = scope->varsp();
    if (!vars)
        throw std::runtime_error(
            "SignalReader: no vars in scope " + scope_name);

    // Find the variable.
    for (const auto& [vn, var] : *vars) {
        if (var_name == vn) {
            datap_   = var.datap();
            n_bits_  = var.entBits();
            n_words_ = (n_bits_ + 31) / 32;
            aval_buf_.assign(n_words_, 0);
            return;
        }
    }

    throw std::runtime_error(
        "SignalReader: cannot resolve signal " + path);
}

// --- read ---

const uint32_t* SignalReader::read() {
    std::memcpy(aval_buf_.data(), datap_, n_words_ * sizeof(uint32_t));

    // Mask off unused bits in the top word.
    if (uint32_t rem = n_bits_ % 32)
        aval_buf_[n_words_ - 1] &= (1u << rem) - 1;

    return aval_buf_.data();
}

// --- bulk discovery ---

std::vector<SignalReader>
discover_all_signals(const std::string& root_scope) {
    const VerilatedScopeNameMap* scopes = get_scope_map();

    std::vector<SignalReader> readers;

    for (const auto& [name, scope] : *scopes) {
        if (std::string(name).rfind(root_scope, 0) != 0)
            continue;

        const VerilatedVarNameMap* vars = scope->varsp();
        if (!vars) continue;

        for (const auto& [vname, var] : *vars) {
            readers.emplace_back(
                SignalReader(var.datap(), var.entBits()));
        }
    }

    if (readers.empty())
        throw std::runtime_error(
            "SignalReader: no signals discovered under " + root_scope);

    return readers;
}

DiscoveredSignals
discover_all_signals_named(const std::string& root_scope) {
    const VerilatedScopeNameMap* scopes = get_scope_map();

    DiscoveredSignals result;

    for (const auto& [name, scope] : *scopes) {
        if (std::string(name).rfind(root_scope, 0) != 0)
            continue;

        const VerilatedVarNameMap* vars = scope->varsp();
        if (!vars) continue;

        for (const auto& [vname, var] : *vars) {
            result.readers.emplace_back(
                SignalReader(var.datap(), var.entBits()));
            result.names.push_back(std::string(name) + "." + std::string(vname));
        }
    }

    if (result.readers.empty())
        throw std::runtime_error(
            "SignalReader: no signals discovered under " + root_scope);

    return result;
}

} // namespace sca
