// File:    can_parser.cpp
// Author:  Blake Hill
// Usage:   Parses CAN messages through the CAN dbc file, create a CanParser object and
//          then pass in CAN_message_t objects for their signals.

#include "can_parser.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

using msgMap = std::unordered_map<uint32_t, const dbcppp::IMessage *>;

CanParser::CanParser() {
    dbc = load_dbc();
    msgMap = loadMessages();
}

CanParser::CanParser(const std::filesystem::path dbc_file_path) : dbc_file_path(dbc_file_path) {
    dbc = load_dbc();
    msgMap = loadMessages();
}

std::unique_ptr<dbcppp::INetwork> CanParser::load_dbc() {
    std::ifstream dbc_file(dbc_file_path);
    if (!dbc_file.is_open()) {
        throw std::runtime_error("Failed to open DBC file:" + dbc_file_path.string());
    }
    std::unique_ptr<dbcppp::INetwork> dbc = dbcppp::INetwork::LoadDBCFromIs(dbc_file);
    if (!dbc) {
        throw std::runtime_error("Failed to load DBC file:" + dbc_file_path.string());
    }
    return dbc;
}

messageMap CanParser::loadMessages() {
    std::unordered_map<uint32_t, const dbcppp::IMessage *> messages;
    for (const dbcppp::IMessage &msg : dbc->Messages()) {
        messages.insert(std::make_pair(static_cast<uint32_t>(msg.Id()), &msg));
    }
    return messages;
}

messageMap CanParser::getMessages() { return msgMap; }

std::optional<SignalMap> CanParser::parse_frame(const CAN_message_t &frame) {
    auto iter = msgMap.find(frame.id);
    if (iter != msgMap.end()) {
        const dbcppp::IMessage *message = iter->second;

        std::unordered_map<std::string, double> sigMap;
        for (const dbcppp::ISignal &sig : message->Signals()) {
            auto raw_sig = sig.Decode(frame.buf);

            sigMap[sig.Name()] = sig.RawToPhys(raw_sig);
        }
        return SignalMap(sigMap);
    } else {
        std::cerr << "ID " << frame.id << " not found\n";
        return std::nullopt;
    }
}

std::optional<CAN_message_t> CanParser::create_frame(
    const uint32_t id, const std::unordered_map<std::string, double> &input_signals
) {
    auto iter = msgMap.find(id);
    if (iter != msgMap.end()) {
        const dbcppp::IMessage *msg = iter->second;

        CAN_message_t can_msg = {};

        // For each signal, put that data into can_msg.
        // If signal not found in input, frame isn't valid.
        auto msgSignals = msg->Signals();
        for (const auto &msgSignal : msgSignals) {
            auto in_sig_iter = input_signals.find(msgSignal.Name());
            if (in_sig_iter != input_signals.end()) { // If signal is found in input_signals
                double in_val = in_sig_iter->second;

                // If in_val is within signal min/max, make msgBuf with it
                if ((in_val >= msgSignal.Minimum()) && (in_val <= msgSignal.Maximum())) {
                    uint64_t raw = msgSignal.PhysToRaw(in_val);
                    msgSignal.Encode(raw, can_msg.buf);
                } else {
                    printf(
                        "Signal: %s, val: %f not in bounds; Min: %f, Max: %f\n", msgSignal.Name(),
                        in_val, msgSignal.Minimum(), msgSignal.Maximum()
                    );
                    return std::nullopt;
                }
            } else {
                std::cerr << "Signal " << msgSignal.Name() << " not in input_signals\n";
                return std::nullopt;
            }
        }

        // If signals are fully complete, fill out the rest of the msg struct and return it.
        can_msg.id = id;
        can_msg.len = static_cast<uint8_t>(msg->MessageSize());
        can_msg.flags.extended = (msg->Id() > 0x7FF);
        can_msg.flags.remote = false; // false = 0
        return can_msg;
    } else {
        std::cerr << "ID " << id << " not found\n";
        return std::nullopt;
    }
}

void CanParser::print_network_info() {
    std::cout << "DBC Version: " << dbc->Version() << "\n";
    std::cout << "Number of Nodes: " << dbc->Nodes_Size() << "\n";
    std::cout << "Number of Messages: " << dbc->Messages_Size() << "\n";
    std::cout << "Number of Value Tables: " << dbc->ValueTables_Size() << "\n";
    std::cout << "Number of Environment Variables: " << dbc->EnvironmentVariables_Size() << "\n";
    std::cout << "Number of Attribute Definitions: " << dbc->AttributeDefinitions_Size() << "\n";
    std::cout << "Number of Attribute Defaults: " << dbc->AttributeDefaults_Size() << "\n";
    std::cout << "Number of Attribute Values: " << dbc->AttributeValues_Size() << "\n";
    std::cout << "Comment: " << dbc->Comment() << "\n";
}

void CanParser::print_messages() {
    try {
        for (const auto &msg : dbc->Messages()) {
            std::cout << msg.Name() << " | ID: " << msg.Id() << " | Bytes: " << msg.MessageSize()
                      << '\n';
            for (const auto &sig : msg.Signals()) {
                std::cout << "  " << sig.Name() << " | Start Bit: " << sig.StartBit()
                          << " | Length: " << sig.BitSize() << " | Factor: " << sig.Factor()
                          << " | Offset: " << sig.Offset() << " | Unit: " << sig.Unit() << '\n';
            }
        }
    } catch (const std::exception &err) {
        std::cerr << "Error: " << err.what() << '\n';
    }
}

int main() {
    CanParser canparse = CanParser();
    // canparse.print_messages();
    CAN_message_t frame{};
    frame.len = 8;
    frame.id = 0xB0;
    frame.buf[0] = 0;
    frame.buf[1] = 0x01;
    frame.buf[2] = 0;
    frame.buf[3] = 0x01;
    frame.buf[4] = 0;
    frame.buf[5] = 0x10;
    frame.buf[6] = 0x10;
    frame.buf[7] = 0;
    auto sigmap = canparse.parse_frame(frame);
    if (!sigmap) {
        return 1;
    }

    std::string name = "TorqueCommand";
    auto val = sigmap->getSignalValue(name);
    if (!val) {
        return 1;
    }
    printf("Signal %s has value %f\n", name.c_str(), *val);
}
