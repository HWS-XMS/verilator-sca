#include "sca/config.h"

#include <stdexcept>
#include <yaml-cpp/yaml.h>

namespace sca {

Config parse_config(const std::string& path) {
    YAML::Node root = YAML::LoadFile(path);
    Config cfg;

    if (root["leakage_model"]) {
        const auto& node = root["leakage_model"];
        if (node.IsScalar())
            cfg.leakage_models = {node.as<std::string>()};
        else if (node.IsSequence()) {
            cfg.leakage_models.clear();
            for (const auto& m : node)
                cfg.leakage_models.push_back(m.as<std::string>());
        }
    }
    if (root["save_per_signal"])
        cfg.save_per_signal = root["save_per_signal"].as<bool>();

    if (!root["signals"])
        throw std::runtime_error("config: 'signals' key is required");

    if (root["signals"].IsScalar() &&
        root["signals"].as<std::string>() == "auto") {
        cfg.auto_discover = true;
        if (!root["scope"])
            throw std::runtime_error(
                "config: 'scope' is required when signals is 'auto'");
        cfg.scope = root["scope"].as<std::string>();
    } else if (root["signals"].IsSequence()) {
        for (const auto& node : root["signals"]) {
            SignalConfig sc;
            sc.path = node["path"].as<std::string>();
            if (node["weight"])
                sc.weight = node["weight"].as<double>();
            cfg.signals.push_back(std::move(sc));
        }
        if (cfg.signals.empty())
            throw std::runtime_error("config: at least one signal required");
    } else {
        throw std::runtime_error(
            "config: 'signals' must be 'auto' or a YAML sequence");
    }

    if (root["experiment"])
        cfg.experiment = root["experiment"].as<std::string>();
    if (root["series"])
        cfg.series = root["series"].as<std::string>();

    return cfg;
}

} // namespace sca
