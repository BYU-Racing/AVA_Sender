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

// ===== DEBUG SETTINGS =====
// Uncomment to enable CAN RX prints
#define DEBUG_CAN_RX

// Optional: print at most N lines per second (keeps it from spamming / slowing)
#define DEBUG_CAN_RX_RATE_LIMIT_HZ 10   // e.g., 50 prints/sec

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
    uint8_t id;
    uint8_t length;
    uint8_t bytes[8];
} __attribute__((packed));

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

#ifdef DEBUG_CAN_RX // AI
namespace dbg_can {

// Basic ANSI colors (works in most terminals; if you hate color, set empty strings)
static constexpr const char* CLR_RESET  = "\033[0m";
static constexpr const char* CLR_DIM    = "\033[2m";
static constexpr const char* CLR_STD    = "\033[36m";  // cyan
static constexpr const char* CLR_EXT    = "\033[35m";  // magenta
static constexpr const char* CLR_ERR    = "\033[31m";  // red
static constexpr const char* CLR_RTR    = "\033[33m";  // yellow


#if defined(DEBUG_CAN_RX_RATE_LIMIT_HZ) && (DEBUG_CAN_RX_RATE_LIMIT_HZ > 0)
static inline bool allow_print()
{
    // Token bucket-ish: allow up to HZ prints per second
    static uint64_t window_start_ms = getTimeNow64();
    static uint32_t count_in_window = 0;

    uint64_t t = getTimeNow64();
    if (t - window_start_ms >= 1000) {
        window_start_ms = t;
        count_in_window = 0;
    }
    if (count_in_window >= (uint32_t)DEBUG_CAN_RX_RATE_LIMIT_HZ) return false;
    ++count_in_window;
    return true;
}
#else
static inline bool allow_print() { return true; }
#endif

static inline void print_frame(const struct can_frame& frame, uint32_t raw_id)
{
    if (!allow_print()) return;

    const bool is_ext = (frame.can_id & CAN_EFF_FLAG) != 0;
    const bool is_rtr = (frame.can_id & CAN_RTR_FLAG) != 0;
    const uint8_t dlc = frame.can_dlc > 8 ? 8 : frame.can_dlc;

    // Build message in a stringstream (single iostream write reduces interleaving/overhead)
    std::ostringstream oss;

    // timestamp
    oss << CLR_DIM << "[" << getTimeNow64() << " ms] " << CLR_RESET;

    // type + id
    oss << (is_ext ? CLR_EXT : CLR_STD)
        << (is_ext ? "EXT " : "STD ")
        << CLR_RESET;

    // raw ID in hex (width depends on standard/extended)
    oss << "ID=0x"
        << std::hex << std::uppercase << std::setfill('0')
        << std::setw(is_ext ? 8 : 3) << raw_id
        << std::dec << std::nouppercase;

    // DLC
    oss << " DLC=" << (int)dlc << " DATA=";

    if (is_rtr) {
        oss << CLR_RTR << "(RTR)" << CLR_RESET;
    } else {
        // bytes
        oss << std::hex << std::uppercase << std::setfill('0');
        for (int i = 0; i < dlc; ++i) {
            oss << std::setw(2) << (int)frame.data[i];
            if (i != dlc - 1) oss << ' ';
        }
        oss << std::dec << std::nouppercase;
    }

    oss << "\n";

    // One output call
    std::cout << oss.str();
}

} // namespace dbg_can
#endif


std::string url = "ws://13.58.232.73:8000/api/ws/send";
const uint64_t RECONNECT_DELAY_MS = 5000; // 5 seconds

static_assert(sizeof(pi_to_server) == 14, "pi_to_server must be 14 bytes"); // Constantly checks that packet is the right size

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
    std::atomic<bool>& was_connected, std::atomic<uint64_t>& reconnect_deadline
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
            std::cout << "Connected to WS" << "\n";
        } else if (msg->type == Type::Message) {
            std::cout << "Received text msg: " << msg->str << "\n";
        } else if (msg->type == Type::Close) {
            ws_open = false;
            if(was_connected && reconnect_deadline == 0){
                reconnect_deadline = getTimeNow64() + RECONNECT_DELAY_MS;
            }
            std::cout << "Close signal received\n";
        } else if (msg->type == Type::Error) {
            ws_open = false;
            if(was_connected && reconnect_deadline == 0){
                reconnect_deadline = getTimeNow64() + RECONNECT_DELAY_MS;
            }
            std::cerr << "WS Error: " << msg->errorInfo.reason << "\n";
        }
    });
}

void readCAN(int can_fd, std::mutex& m, std::queue<pi_to_server>& q, std::atomic<bool>& running){
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

        // Extract raw CAN identifier (strip flags)
        uint32_t raw_id = 0;
        if (frame.can_id & CAN_EFF_FLAG) {
            raw_id = (frame.can_id & CAN_EFF_MASK);   // 29-bit extended
        } else {
            raw_id = (frame.can_id & CAN_SFF_MASK);   // 11-bit standard
        }

        #ifdef DEBUG_CAN_RX
        dbg_can::print_frame(frame, raw_id);
        #endif

        pkt.id = raw_id;

        pkt.length = frame.can_dlc;
        if (pkt.length > 8) pkt.length = 8;
        std::memcpy(pkt.bytes, frame.data, pkt.length);

        {
            std::lock_guard<std::mutex> lk(m);
            q.push(pkt);
        }
    }
}


int main() {
    ix::WebSocket webSocket;
    std::atomic<bool> ws_open{false};
    std::atomic<bool> was_connected{false};
    std::atomic<uint64_t> reconnect_deadline{0};
    setupWebSocket(webSocket, ws_open, was_connected, reconnect_deadline);

    // CAN queue
    std::mutex m;
    std::queue<pi_to_server> q;
    std::atomic<bool> running{true}; // Atomic so that all threads can read it safely
    

    webSocket.start();

    int can0_fd = openCANSocket("can0");
    if(can0_fd < 0) {
        std::cerr << "Failed to open CAN0 socket\n";
        return 1;
    }

    // int can1_fd = openCANSocket("can1");
    // if(can1_fd < 0) {
    //     std::cerr << "Failed to open CAN1 socket\n";
    //     return 1;
    // }

    // CAN0 reader thread
    std::thread can_thread([&](){
        readCAN(can0_fd, m, q, running);
    });

    // std::thread can1_thread([&](){
    //     readCAN(can1_fd, m, q, running);
    // })

    while(webSocket.getReadyState() != ix::ReadyState::Open) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Sender loop
    std::cout << "Sending data...\nPress Ctrl+C to quit\n";
    while (true) { 
        if(!ws_open){
            // check if reconnect deadline has passed
            if(was_connected && 
               reconnect_deadline != 0 && 
               getTimeNow64() >= reconnect_deadline) {
                std::cout << "Websocket closed.\n";
                break;
            }
            // Try to restart websocket if it's not open
            if(was_connected) {
                webSocket.stop();
                webSocket.start(); 
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        pi_to_server pkt {};
        bool unlocked = false;
        {
            std::lock_guard<std::mutex> lock(m);
            if (!q.empty()){
                pkt = q.front();
                q.pop();
                unlocked = true;
            }
        }
        if(!unlocked){ // if queue not ready, wait 100ms and try again
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        std::string payload(sizeof(pkt), '\0');
        std::memcpy(payload.data(), &pkt, sizeof(pkt));
        webSocket.sendBinary(payload);
    }
    
    running = false;
    close(can_fd);
    can_thread.join();
    webSocket.stop();
    ix::uninitNetSystem();

    return 0;
}
