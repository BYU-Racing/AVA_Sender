#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>

#include <boost/circular_buffer.hpp>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "ublox_gnss.h"

// MARK: Namespace
using std::uint16_t;
using std::uint32_t;
using std::uint64_t;
using std::uint8_t;

using std::int16_t;
using std::int32_t;
using std::int64_t;
using std::int8_t;

using std::atomic;
using std::fprintf;
using std::printf;
using std::size_t;
using std::thread;

// MARK: Structs/Consts
#define UBX_BYTE_1 0xB5
#define UBX_BYTE_2 0x62
#define CIRC_BUF_OVERHEAD 8
#define CFG_PAYLOAD_SIZE 34

const std::string url_prefix = "ws://";
const std::string url_suffix = ":8000/api/ws/send";
const uint64_t RECONNECT_DELAY_MS = 10000; // 10 seconds, max time trying to reconnect
const uint64_t RETRY_INTERVAL_MS = 1000;   // 1 second interval between reconnect attempts
const uint64_t RESEND_INTERVAL_MS = 50;    // 50 ms interval between resending failed messages
const uint16_t NUM_PACKET_RETRIES = 20;    // Retries sending a packet this many times, then closes
const size_t GNSS_BUF_SIZE = 2048;         // Buffer size for reading GNSS data

/*  Data struct for sending CAN frame to server, packed to avoid padding.
    Struct for queued packet as well for retrying failed sends.
{
    timestamp: int,
    id: int,
    length: int, // up to 8
    bytes: List[int], // length: up to 8
}
*/
struct pi_to_server {
    uint32_t timestamp;
    uint32_t id;
    uint8_t length;
    uint8_t bytes[8];
} __attribute__((packed));

struct queued_packet {
    pi_to_server pkt;
    uint64_t last_send_attempt;
    uint16_t retries;
};

// Non-Const Global Variables
volatile std::sig_atomic_t stop_requested = 0;

// MARK: Helper Funcs
// Monotonic timestamp in ms (relative to start)
static inline uint64_t getTimeNow64() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static uint32_t getTimeNow32() {
    using namespace std::chrono;
    return static_cast<uint32_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count()
    );
}

void handleSignal(int) { stop_requested = 1; }

std::string getEnvVar(const char *name) {
    const char *value = std::getenv(name);

    if (value == nullptr || *value == '\0') {
        throw std::runtime_error(std::string("Environment variable not set: ") + name);
    }

    return std::string(value);
}

// MARK: Main Code
static_assert(
    sizeof(pi_to_server) == 17, "pi_to_server must be 17 bytes"
); // Constantly checks that packet is the right size

// Opens a CAN socket on the specified interface and returns the socket file descriptor. Returns -1
// on failure.
static int openCANSocket(const char *ifname) {
    int s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) {
        perror("Problem opening CAN socket");
        return -1;
    }

    struct ifreq ifr{};
    std::strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
        perror("ioctl(SIOCGIFINDEX)");
        close(s);
        return -1;
    }

    struct sockaddr_can addr{};
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind(can)");
        close(s);
        return -1;
    }
    return s;
}

// Sets up websocket with URL and sets up message callback function
void setupWebSocket(
    ix::WebSocket &webSocket, const std::string &url, atomic<bool> &ws_open,
    atomic<bool> &was_connected, atomic<uint64_t> &reconnect_deadline,
    atomic<uint64_t> &next_reconnect_attempt
) {
    // Closes WS and sets reconnect_deadline
    auto close_ws = [&]() {
        ws_open = false;
        if (was_connected && reconnect_deadline == 0) {
            reconnect_deadline = getTimeNow64() + RECONNECT_DELAY_MS;
            next_reconnect_attempt = getTimeNow64();
        }
    };

    ix::initNetSystem();

    webSocket.setUrl(url);
    webSocket.disableAutomaticReconnection();
    webSocket.setOnMessageCallback([&](const ix::WebSocketMessagePtr &msg) {
        using Type = ix::WebSocketMessageType;

        if (msg->type == Type::Open) {
            ws_open = true;
            was_connected = true;
            reconnect_deadline = 0;
            next_reconnect_attempt = 0;
            printf("Connected to WS\n");
        } else if (msg->type == Type::Message) {
            printf("Received text msg: %s\n", (msg->str).c_str());
        } else if (msg->type == Type::Close) {
            close_ws();
            printf("Close signal received\n");
        } else if (msg->type == Type::Error) {
            close_ws();
            fprintf(stderr, "WS Error: %s\n", msg->errorInfo.reason);
        } else if (msg->type == Type::Ping) {
            printf("Received ping %s\n", (msg->str).c_str());
        } else if (msg->type == Type::Pong) {
            printf("Received pong %s\n", (msg->str).c_str());
        }
    });
}

// MARK: GNSS Reader
void gnssReader(
    int gnss_fd, const std::string &base_station_url, std::mutex &m, std::queue<queued_packet> &q,
    atomic<bool> &running
) {
    if (!config_gnss(gnss_fd)) {
        std::perror("Failed to configure GNSS socket");
        return;
    }

    boost::circular_buffer<uint8_t> data_buf(GNSS_BUF_SIZE); // Ring buffer to read in data
    std::vector<uint8_t> ubx_frame;                          // Vector for extracted UBX frame

    ix::WebSocket bsWebSocket;
    bsWebSocket.setUrl(base_station_url);
    bsWebSocket.enableAutomaticReconnection();
    bsWebSocket.setOnMessageCallback([&](const ix::WebSocketMessagePtr &msg) {
        using Type = ix::WebSocketMessageType;

        if (msg->type == Type::Open) {
            printf("Connected to Base Station WS\n");
        } else if (msg->type == Type::Message) {
            std::vector<uint8_t> rtk_corrections(msg->str.begin(), msg->str.end());
            writeRTKCorrections(gnss_fd, rtk_corrections);
        } else if (msg->type == Type::Close) {
            printf("Base Station WS closed\n");
        } else if (msg->type == Type::Error) {
            fprintf(stderr, "Base Station WS Error: %s\n", msg->errorInfo.reason);
        }
    });

    while (running) {
        uint16_t freespace = data_buf.capacity() - data_buf.size();
        if (freespace == 0) {
            continue; // Buffer is full, wait for processing
        }
        ssize_t bytes_read = read(gnss_fd, data_buf.data(), freespace);
        if (bytes_read > 0) { // Process the received GNSS data
            if (tryExtractUbxFrame(data_buf, ubx_frame)) {
                // Process the extracted UBX frame
                handleUbxMessage(ubx_frame);
            }
        } else if (bytes_read == 0) { // Read no bytes
            continue;
        } else if (bytes_read < 0) { // Error reading from GNSS device
            if (errno == EINTR) {
                continue; // Interrupted by signal, retry reading
            } else {
                if (!running)
                    break;
                perror("read(gnss)");
                continue;
            }
        }
    }
}

// MARK: CAN Reader
void readCAN(int can_fd, std::mutex &m, std::queue<queued_packet> &q, atomic<bool> &running) {
    while (running) {
        struct can_frame frame{};
        int n = read(can_fd, &frame, sizeof(frame));
        if (n < 0) {
            if (!running)
                break;
            perror("read(can)");
            continue;
        }
        if (n != (int)sizeof(frame))
            continue;

        pi_to_server pkt{};
        pkt.timestamp = getTimeNow32();

        // Ignore error frames
        if (frame.can_id & CAN_ERR_FLAG) {
            continue;
        }

        // Extract raw CAN identifier (strip flags)
        uint32_t raw_id = 0;
        if (frame.can_id & CAN_EFF_FLAG) {
            raw_id = (frame.can_id & CAN_EFF_MASK); // 29-bit extended
        } else {
            raw_id = (frame.can_id & CAN_SFF_MASK); // 11-bit standard
        }

        pkt.id = raw_id;

        pkt.length = frame.can_dlc;
        if (pkt.length > 8)
            pkt.length = 8;
        std::memcpy(pkt.bytes, frame.data, pkt.length);

        queued_packet queued{};
        queued.pkt = pkt;
        queued.last_send_attempt = 0;
        queued.retries = 0;

        {
            std::lock_guard<std::mutex> lk(m);
            q.push(queued);
        }
    }
}

// MARK: Main Function
int main() {
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    std::string server_ip;
    std::string base_station_ip;
    try {
        server_ip = getEnvVar("AVA_SERVER_IP");
        base_station_ip = getEnvVar("AVA_BASE_STATION_IP");
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    const std::string url = url_prefix + server_ip + url_suffix;
    const std::string base_station_url = url_prefix + base_station_ip + url_suffix;

    // Websocket setup
    ix::WebSocket webSocket;
    atomic<bool> ws_open{false};
    atomic<bool> was_connected{false};
    atomic<uint64_t> reconnect_deadline{0};
    atomic<uint64_t> next_reconnect_attempt{0};
    setupWebSocket(
        webSocket, url, ws_open, was_connected, reconnect_deadline, next_reconnect_attempt
    );

    // CAN queue
    std::mutex m;
    std::queue<queued_packet> q;
    atomic<bool> running{true}; // Atomic so that all threads can read it safely

    webSocket.start();

    // first reconnect attempt is RETRY_INTERVAL_MS after start
    next_reconnect_attempt = getTimeNow64() + RETRY_INTERVAL_MS;

    // open file desc for CAN0
    int can0_fd = openCANSocket("can0");
    if (can0_fd < 0) {
        std::perror("Failed to open CAN0 socket");
        return 1;
    }

    // MARK: FDs & Threads
    // open file desc for CAN1
    int can1_fd = openCANSocket("can1");
    if (can1_fd < 0) {
        std::perror("Failed to open CAN1 socket");
        return 1;
    }

    int gnss_fd = open("/dev/ttyACM0", O_RDWR | O_NOCTTY);
    if (gnss_fd < 0) {
        std::perror("Failed to open GNSS socket");
        return 1;
    }
    if (!setupGnssFd(gnss_fd)) {
        std::perror("Failed to setup GNSS socket");
        return 1;
    }

    // CAN0 reader thread
    thread can0_thread([&]() { readCAN(can0_fd, m, q, running); });

    // CAN1 reader thread
    thread can1_thread([&]() { readCAN(can1_fd, m, q, running); });

    // GNSS reader thread
    thread gnss_reader_thread([&]() { gnssReader(gnss_fd, base_station_url, m, q, running); });

    // MARK: Sender Loop
    printf("Starting sender loop...\nPress Ctrl+C to quit\n");
    while (!stop_requested) {

        // -- Reconnect logic --
        if (!ws_open) {

            // check if reconnect deadline has passed, close program if so
            if (reconnect_deadline != 0 && getTimeNow64() >= reconnect_deadline) {
                fprintf(stderr, "Websocket closed.\n");
                break;
            }

            // Try to restart websocket if it's not open when next_reconnect_attempt has passed
            if (next_reconnect_attempt != 0 && getTimeNow64() >= next_reconnect_attempt) {
                webSocket.stop();
                webSocket.start();
                next_reconnect_attempt = getTimeNow64() + RETRY_INTERVAL_MS;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        // Pop packet from queue for sending
        queued_packet q_pkt{};
        bool unlocked = false;
        {
            std::lock_guard<std::mutex> lock(m);

            if (!q.empty()) { // if queue isn't empty, pop front pkt
                q_pkt = q.front();
                uint64_t now = getTimeNow64();
                if ((q_pkt.last_send_attempt == 0) ||
                    (now - q_pkt.last_send_attempt >= RESEND_INTERVAL_MS)) {
                    q.front().last_send_attempt = now;
                    unlocked = true;
                }
            }
        }
        if (!unlocked) { // if queue not ready, wait 10ms and try again
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // Send pkt as binary over websocket
        std::string payload(sizeof(q_pkt.pkt), '\0');
        std::memcpy(payload.data(), &q_pkt.pkt, sizeof(q_pkt.pkt));

        auto info = webSocket.sendBinary(payload);
        if (info.success) { // If sent successfully, pop from queue
            std::lock_guard<std::mutex> lock(m);
            if (!q.empty()) {
                q.pop();
            }
        } else { // If failed to send, retry; if too many retries, reconnect
            std::lock_guard<std::mutex> lock(m);
            if (q.front().retries++ >= NUM_PACKET_RETRIES) {
                fprintf(
                    stderr, "Failed to send packet after %d retries. Reconnecting.\n",
                    NUM_PACKET_RETRIES
                );
                q.front().retries = 0;
                q.front().last_send_attempt = 0;
                ws_open = false;
                webSocket.stop();
                continue;
            }
            fprintf(stderr, "Failed to send packet\n");
        }
    }

    // MARK: Cleanup
    running = false;
    shutdown(can0_fd, SHUT_RD);
    shutdown(can1_fd, SHUT_RD);
    close(can0_fd);
    close(can1_fd);
    close(gnss_fd);
    can0_thread.join();
    can1_thread.join();
    gnss_reader_thread.join();
    webSocket.stop();
    ix::uninitNetSystem();

    return 0;
}
