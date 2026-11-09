#pragma once

#include <optional>
#include <string>
#include <unordered_map>

class SignalMap {
  public:
    SignalMap(std::unordered_map<std::string, double> sig_map);

    // Gets data held by signal from signal name
    std::optional<double> getSignalValue(std::string sig_name);

    // Gets signals from SignalMap struct
    std::unordered_map<std::string, double> getSignals() { return sig_map; }

  private:
    std::unordered_map<std::string, double> sig_map;
};