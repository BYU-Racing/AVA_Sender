#pragma once

#include <cstdint>
#include <map>
#include <string.h>
#include <string>
#include <utility>

#include "can_parser.hpp"

/*  Data struct for sending CAN frame to server, packed to avoid padding.
    Struct for queued packet as well for retrying failed sends.
{
    timestamp: int, // 4
    signal_id: int, // 2
    data: double, // 8
}
*/
struct pi_to_server {
    std::uint32_t timestamp;
    std::uint16_t sig_id;
    float data;
} __attribute__((packed)); // Length: 10 bytes

// Create signal ID mapping
using SignalKey = std::pair<uint32_t, std::string>;
using SignalIDMap = std::map<SignalKey, uint16_t>;

SignalIDMap makeSignalMap(CanParser &parser) {
    SignalIDMap id_map;
    for (const auto &[canId, message] : parser) {
        for (const auto &signal : message) {
            map.emplace(SignalKey{canId, signal.Name()}, 0);
        }
    }

    // std::map sorts by CAN ID, then signal name
    uint16_t curID = 1;
    for (auto &[key, signalId] : id_map) {
        signalId = curID++;
    }

    return id_map;
}
