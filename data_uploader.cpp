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
    uint8_t id; //TODO: id could be bigger
    uint8_t length;
    uint8_t bytes[8];
} __attribute__((packed));


std::string url = "ws://13.58.232.73:8000/api/ws/send";

static_assert(sizeof(pi_to_server) == 14, "pi_to_server must be 14 bytes");


static uint32_t getTimeNow() {
    using namespace std::chrono;
    return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
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

void setupWebSocket(ix::WebSocket& webSocket, std::atomic<bool>& ws_open) {
    ix::initNetSystem();

    webSocket.setUrl(url);

    webSocket.disableAutomaticReconnection(); // start simple
    

    webSocket.setOnMessageCallback([&](const ix::WebSocketMessagePtr& msg) {
        using Type = ix::WebSocketMessageType;

        if (msg->type == Type::Open) {
            ws_open = true;
            std::cout << "Connected" << "\n";
        } else if (msg->type == Type::Message) {
            std::cout << "Received text msg: " << msg->str << "\n";
        } else if (msg->type == Type::Close) {
            ws_open = false;
            std::cout << "Closed\n";
        } else if (msg->type == Type::Error) {
            ws_open = false;
            std::cerr << "Error: " << msg->errorInfo.reason << "\n";
        }
    });
}

void fill(pi_to_server* pkt, uint32_t timestamp, uint8_t id, uint8_t length) { // Fill packets
    pkt->timestamp = timestamp;
    pkt->id = id;
    pkt->length = length;
}

int main() {
    ix::WebSocket webSocket;
    std::atomic<bool> ws_open{false};
    setupWebSocket(webSocket, ws_open);

    // CAN queue
    std::mutex m;
    std::queue<pi_to_server> q;
    std::atomic<bool> running{true}; // Atomic so that all threads can read it safely
    

    webSocket.start();

    int can_fd = openCANSocket("can0");
    if(can_fd < 0) {
        std::cerr << "Failed to open CAN socket\n";
        return 1;
    }

    // CAN reader thread
    std::thread can_thread([&](){
        while (running) {
            struct can_frame frame {};
            int n = read(can_fd, &frame, sizeof(frame));
            if (n < 0) {
                perror("read(can)");
                continue;
            }
            if (n != (int)sizeof(frame)) continue;

            pi_to_server pkt {};
            pkt.timestamp = getTimeNow();

            // Extract ID (strip flags)
            uint32_t can_id = frame.can_id;
            uint8_t id = (can_id & CAN_EFF_FLAG) ? (can_id & CAN_EFF_MASK) : (can_id & CAN_SFF_MASK);
            pkt.id = id;

            pkt.length = frame.can_dlc;
            if (pkt.length > 8) pkt.length = 8;
            std::memcpy(pkt.bytes, frame.data, pkt.length);

            {
                std::lock_guard<std::mutex> lk(m);
                q.push(pkt);
            }
        }
    });


    while(webSocket.getReadyState() != ix::ReadyState::Open) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "Sending data...\nPress Ctrl+C to quit\n";
    while (true) { 
        if(!ws_open){
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
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
    can_thread.join();
    close(can_fd);
    webSocket.stop();
    ix::uninitNetSystem();

    return 0;
}