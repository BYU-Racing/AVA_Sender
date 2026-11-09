// File:    signal_map.cpp
// Author:  Blake Hill
// Usage:   Provides signal map object and signal decoding for CAN parsing.

#include "signal_map.hpp"

SignalMap::SignalMap(std::unordered_map<std::string, double> sig_map) : sig_map(sig_map) {}

std::optional<double> SignalMap::getSignalValue(std::string sig_name) {
    if (sig_map.find(sig_name) != sig_map.end()) {
        return sig_map[sig_name];
    } else {
        fprintf(stderr, "Signal %s not found", sig_name.c_str());
        return std::nullopt;
    }
}