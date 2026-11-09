#pragma once

#include "CAN_message_t.h"
#include "signal_map.hpp"
#include <cstdint>
#include <dbcppp/Network.h>
#include <filesystem>
#include <memory>
#include <optional>
#include <unordered_map>

using messageMap = std::unordered_map<uint32_t, const dbcppp::IMessage *>;

class CanParser {
  public:
    CanParser();
    CanParser(const std::filesystem::path dbc_file_path);

    // Pass in a CAN frame and messages, get dict of decoded signals out
    std::optional<SignalMap> parse_frame(const CAN_message_t &frame);

    // Pass in an ID and a map of signals, get a CAN_message_t object back
    std::optional<CAN_message_t>
    create_frame(const uint32_t id, const std::unordered_map<std::string, double> &input_signals);

    // Get Message Map
    messageMap getMessages();

    // Prints general info about DBC file
    void print_network_info();

    // Prints info for all messages
    void print_messages();

  private:
    const std::filesystem::path dbc_file_path = "BYU_Racing_CAN.dbc";
    std::unique_ptr<dbcppp::INetwork> dbc;
    messageMap msgMap;

    // Loads the DBC data from the file path
    std::unique_ptr<dbcppp::INetwork> load_dbc();

    // Pass in a DBC INetwork object, get dict of messages out
    messageMap loadMessages();
};