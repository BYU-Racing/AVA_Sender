#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXNetSystem.h>

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <cstdint>
#include <cstring>
#include <string>
#include <atomic>
#include <chrono>
#include <mutex>
#include <queue>
#include <thread>
#include <iomanip>
#include <sstream>
#include <csignal>
#include <fstream>

// ===== Structs and Constants =====
std::string url = "ws://100.85.246.127:8000/api/ws/send";
const uint64_t RECONNECT_DELAY_MS = 10000; // 10 seconds, max time trying to reconnect
const uint64_t RETRY_INTERVAL_MS = 1000; // 1 second interval between reconnect attempts
const uint64_t RESEND_INTERVAL_MS = 50; // 50 ms interval between resending failed messages
const uint16_t NUM_PACKET_RETRIES = 20; // Retries sending a packet this many times, then closes

/*
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

// ===== Global Variables =====
volatile std::sig_atomic_t stop_requested = 0;


// ===== Helper Functions =====

// Monotonic timestamp in ms (relative to start)
static inline uint64_t getTimeNow64()
{
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static uint32_t getTimeNow32() {
    using namespace std::chrono;
    return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

void handleSignal(int) {
    stop_requested = 1;
}

// ===== Main code =====
static_assert(sizeof(pi_to_server) == 17, "pi_to_server must be 17 bytes"); // Constantly checks that packet is the right size

void writeASCFrame(
    std::ofstream& file,
    std::mutex& file_mutex,
    int channel,
    const can_frame& frame,
    uint64_t start_time_ms
) {
    uint64_t now_ms = getTimeNow64();
    double timestamp = (now_ms - start_time_ms) / 1000.0;

    uint32_t raw_id = frame.can_id & CAN_EFF_FLAG
        ? frame.can_id & CAN_EFF_MASK
        : frame.can_id & CAN_SFF_MASK;

    std::lock_guard<std::mutex> lock(file_mutex);

    file << std::fixed << std::setprecision(6)
         << timestamp << " "
         << channel << " "
         << std::hex << std::uppercase << raw_id
         << std::dec << " Rx d "
         << (int)frame.can_dlc;

    for (int i = 0; i < frame.can_dlc && i < 8; i++) {
        file << " "
             << std::hex << std::uppercase
             << std::setw(2) << std::setfill('0')
             << (int)frame.data[i];
    }

    file << std::dec << std::setfill(' ') << "\n";
}

static int openCANSocket(const char* ifname){
    int s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if(s < 0){
        perror("Problem opening CAN socket");
        return -1;
    }

    struct ifreq ifr {};
    std::strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) { perror("ioctl(SIOCGIFINDEX)"); close(s); return -1; }

    struct sockaddr_can addr {};
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) { 
        perror("bind(can)"); close(s); 
        return -1; 
    }
    return s;
}

// Sets up websocket with URL and sets up message callback function
void setupWebSocket(
    ix::WebSocket& webSocket, std::atomic<bool>& ws_open, 
    std::atomic<bool>& was_connected, std::atomic<uint64_t>& reconnect_deadline,
    std::atomic<uint64_t>& next_reconnect_attempt
    ) {

    ix::initNetSystem();

    webSocket.setUrl(url);

    webSocket.disableAutomaticReconnection(); 

    webSocket.setOnMessageCallback([&](const ix::WebSocketMessagePtr& msg) {
        using Type = ix::WebSocketMessageType;

        if (msg->type == Type::Open) {
            ws_open = true;
            was_connected = true;
            reconnect_deadline = 0;
            next_reconnect_attempt = 0;
            std::cout << "Connected to WS" << "\n";
        } else if (msg->type == Type::Message) {
            std::cout << "Received text msg: " << msg->str << "\n";
        } else if (msg->type == Type::Close) {
            ws_open = false;
            if(was_connected && reconnect_deadline == 0){
                reconnect_deadline = getTimeNow64() + RECONNECT_DELAY_MS;
                next_reconnect_attempt = getTimeNow64();
            }
            std::cout << "Close signal received\n";
        } else if (msg->type == Type::Error) {
            ws_open = false;
            if(was_connected && reconnect_deadline == 0){
                reconnect_deadline = getTimeNow64() + RECONNECT_DELAY_MS;
                next_reconnect_attempt = getTimeNow64();
            }
            std::cerr << "WS Error: " << msg->errorInfo.reason << "\n";
        }
    });
}

void readCAN(
    int can_fd, std::string can_str, 
    std::mutex& m, std::queue<queued_packet>& q, 
    std::atomic<bool>& running, std::ofstream& asc_file, 
    std::mutex& asc_mutex, uint64_t start_time_ms
    ) {
    while (running) {
        struct can_frame frame {};
        int n = read(can_fd, &frame, sizeof(frame));
        if (n < 0) {
            perror("read(can)");
            continue;
        }
        if (n != (int)sizeof(frame)) continue;

        pi_to_server pkt {};
        pkt.timestamp = getTimeNow32();

        // Ignore error frames
        if (frame.can_id & CAN_ERR_FLAG) {
            continue;
        }

        int channel = can_str == "can0" ? 1 : 2;
        writeASCFrame(asc_file, asc_mutex, channel, frame, start_time_ms);

        // Extract raw CAN identifier (strip flags)
        uint32_t raw_id = 0;
        if (frame.can_id & CAN_EFF_FLAG) {
            raw_id = (frame.can_id & CAN_EFF_MASK);   // 29-bit extended
        } else {
            raw_id = (frame.can_id & CAN_SFF_MASK);   // 11-bit standard
        }


        pkt.id = raw_id;

        pkt.length = frame.can_dlc;
        if (pkt.length > 8) pkt.length = 8;
        std::memcpy(pkt.bytes, frame.data, pkt.length);

        queued_packet queued {};
        queued.pkt = pkt;
        queued.last_send_attempt = 0;
        queued.retries = 0;

        {
            std::lock_guard<std::mutex> lk(m);
            q.push(queued);
        }
    }
}


int main() {
    std::signal(SIGINT, handleSignal);
    uint64_t start_time_ms = getTimeNow64();

    // Websocket setup
    ix::WebSocket webSocket;
    std::atomic<bool> ws_open{false};
    std::atomic<bool> was_connected{false};
    std::atomic<uint64_t> reconnect_deadline{0};
    std::atomic<uint64_t> next_reconnect_attempt{0};
    setupWebSocket(webSocket, ws_open, was_connected, reconnect_deadline, next_reconnect_attempt);

    // CAN queue
    std::mutex m;
    std::queue<queued_packet> q;
    std::atomic<bool> running{true}; // Atomic so that all threads can read it safely

    // ASC file setup
    std::ofstream asc_file("can_log.asc");
    std::mutex asc_mutex;

    webSocket.start();

    // first reconnect attempt is RETRY_INTERVAL_MS after start
    next_reconnect_attempt = getTimeNow64() + RETRY_INTERVAL_MS; 

    // open file desc for CAN0
    int can0_fd = openCANSocket("can0");
    if(can0_fd < 0) {
        std::cerr << "Failed to open CAN0 socket\n";
        return 1;
    }

    // open file desc for CAN1
    int can1_fd = openCANSocket("can1");
    if(can1_fd < 0) {
        std::cerr << "Failed to open CAN1 socket\n";
        return 1;
    }

    // CAN0 reader thread
    std::thread can0_thread([&](){
        readCAN(can0_fd, "can0", m, q, running, asc_file, asc_mutex, start_time_ms);
    });

    // CAN1 reader thread
    std::thread can1_thread([&](){
        readCAN(can1_fd, "can1", m, q, running, asc_file, asc_mutex, start_time_ms);
    });


    // Sender loop
    std::cout << "Starting sender loop...\nPress Ctrl+C to quit\n";
    while (!stop_requested) {

        // -- Reconnect logic --
        if(!ws_open){

            // check if reconnect deadline has passed, close program if so
            if(reconnect_deadline != 0 && 
               getTimeNow64() >= reconnect_deadline) {
                std::cout << "Websocket closed.\n";
                break;
            }

            // Try to restart websocket if it's not open when next_reconnect_attempt has passed
            if(next_reconnect_attempt != 0 && 
               getTimeNow64() >= next_reconnect_attempt){
                webSocket.stop();
                webSocket.start(); 
                next_reconnect_attempt = getTimeNow64() + RETRY_INTERVAL_MS;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        // Pop packet from queue for sending
        queued_packet q_pkt {};
        bool unlocked = false;
        {
            std::lock_guard<std::mutex> lock(m);

            if (!q.empty()){ // if queue isn't empty, pop front pkt
                q_pkt = q.front();
                uint64_t now = getTimeNow64();
                if((q_pkt.last_send_attempt == 0) ||
                    (now - q_pkt.last_send_attempt >= RESEND_INTERVAL_MS)) {
                        q.front().last_send_attempt = now;
                        unlocked = true;
                    }
            }
        }
        if(!unlocked){ // if queue not ready, wait 10ms and try again
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // Send pkt as binary over websocket
        std::string payload(sizeof(q_pkt.pkt), '\0');
        std::memcpy(payload.data(), &q_pkt.pkt, sizeof(q_pkt.pkt));

        auto info = webSocket.sendBinary(payload);
        if (info.success){
            std::lock_guard<std::mutex> lock(m);
            if (!q.empty()) {
                q.pop();
            }
        } else {
            if(q.front().retries++ >= NUM_PACKET_RETRIES){
                std::cerr << "Failed to send packet after " << NUM_PACKET_RETRIES << " retries. Reconnecting.\n";
                ws_open = false;
                webSocket.stop();
                continue;
            }
            std::cerr << "Failed to send packet\n";
        }
    }
    
    // Cleanup
    running = false;
    shutdown(can0_fd, SHUT_RD);
    shutdown(can1_fd, SHUT_RD);
    close(can0_fd);
    close(can1_fd);
    can0_thread.join();
    can1_thread.join();
    webSocket.stop();
    ix::uninitNetSystem();

    return 0;
}
