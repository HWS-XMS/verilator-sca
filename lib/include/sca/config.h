#pragma once

#include <string>
#include <vector>

namespace sca {

struct SignalConfig {
    std::string path;
    double weight = 1.0;
};

struct Config {
    std::vector<std::string> leakage_models = {"hamming_distance"};
    bool save_per_signal = false;
    bool auto_discover = false;
    std::string scope;
    std::vector<SignalConfig> signals;
    std::string experiment = "default";
    std::string series     = "default";
};

Config parse_config(const std::string& path);

} // namespace sca
