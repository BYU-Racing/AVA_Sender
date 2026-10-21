#include <cerrno>
#include <cstdio>
#include <cstring>
#include <utility>

#include <termios.h>
#include <unistd.h>

#include "ublox_gnss.h"

// MARK: UBX Constants
constexpr std::size_t UBX_HEADER_SIZE = 6;
constexpr std::size_t UBX_CHECKSUM_SIZE = 2;
constexpr std::size_t UBX_FRAME_OVERHEAD = UBX_HEADER_SIZE + UBX_CHECKSUM_SIZE;

// MARK: NAV-PVT Constants

constexpr std::size_t NAV_PVT_PAYLOAD_SIZE = 92;

// Offsets within the NAV-PVT payload
constexpr std::size_t PVT_FLAGS3_OFFSET = 78;
constexpr std::size_t PVT_FIX_TYPE_OFFSET = 20;
constexpr std::size_t PVT_FLAGS_OFFSET = 21;
constexpr std::size_t PVT_NUM_SATELLITES_OFFSET = 23;
constexpr std::size_t PVT_LONGITUDE_OFFSET = 24;
constexpr std::size_t PVT_LATITUDE_OFFSET = 28;
constexpr std::size_t PVT_HEIGHT_MSL_OFFSET = 36;
constexpr std::size_t PVT_HORIZONTAL_ACCURACY_OFFSET = 40;
constexpr std::size_t PVT_GROUND_SPEED_OFFSET = 60;
constexpr std::size_t PVT_HEADING_OFFSET = 64;

// Validity flags
constexpr uint8_t PVT_INVALID_LLH_MASK = 0x01;

// Navigation flags
constexpr uint8_t PVT_GNSS_FIX_OK_MASK = 0x01;
constexpr uint8_t PVT_DIFFERENTIAL_SOLUTION_MASK = 0x02;

// RTK carrier-solution flags
constexpr uint8_t PVT_CARRIER_SOLUTION_MASK = 0xC0;
constexpr unsigned PVT_CARRIER_SOLUTION_SHIFT = 6;

// Conversion factors
constexpr float PVT_COORDINATE_SCALE = 0.0000001;
constexpr double PVT_MILLIMETERS_TO_METERS = 0.001;
constexpr double PVT_HEADING_SCALE = 0.00001;

// NAV-PVT fix types
enum class PVT_FixType : uint8_t {
    NO_FIX = 0,
    DEAD_RECKONING = 1,
    FIX_2D = 2,
    FIX_3D = 3,
    GNSS_DEAD_RECKONING = 4,
    TIME_ONLY = 5
};

// NAV-PVT RTK statuses
enum class PVT_CarrierSolution : uint8_t { NO_RTK = 0, RTK_FLOAT = 1, RTK_FIXED = 2 };

// MARK: Help Funcs

static void appendU32LE(std::vector<uint8_t> &vec, std::uint32_t value) {
    vec.push_back(static_cast<std::uint8_t>(value & 0xFF));
    vec.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    vec.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    vec.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
}

static bool validateUbxChecksum(std::vector<std::uint8_t> frame, std::uint16_t length) {
    uint8_t CHECKSUM_A = frame[6 + length];
    uint8_t CHECKSUM_B = frame[7 + length];
    uint8_t ck_a = 0;
    uint8_t ck_b = 0;
    for (size_t i = 2; i < length + 6; i++) {
        ck_a += frame[i];
        ck_b += ck_a;
    }
    return (ck_a == CHECKSUM_A && ck_b == CHECKSUM_B);
}

static std::uint16_t readUInt16LE(const std::uint8_t *data) {
    return static_cast<std::uint16_t>(data[0]) | (static_cast<std::uint16_t>(data[1]) << 8);
}

static std::uint32_t readUInt32LE(const std::uint8_t *data) {
    return static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) |
           (static_cast<std::uint32_t>(data[3]) << 24);
}

static std::int32_t readInt32LE(const std::uint8_t *data) {
    return static_cast<std::int32_t>(readUInt32LE(data));
}

// MARK: User Funcs

bool setupGnssFd(int gnss_fd) {
    struct termios settings{};
    int res = tcgetattr(gnss_fd, &settings);
    if (res < 0) {
        return false;
    }
    cfmakeraw(&settings);
    settings.c_cflag &= ~(PARENB | CSTOPB | CSIZE | CRTSCTS);
    settings.c_cflag |= CS8 | CLOCAL | CREAD;
    settings.c_cc[VMIN] = 0;
    settings.c_cc[VTIME] = 10;
    res = cfsetispeed(&settings, B115200);
    if (res < 0) {
        return false;
    }
    res = cfsetospeed(&settings, B115200);
    if (res < 0) {
        return false;
    }
    res = tcsetattr(gnss_fd, TCSANOW, &settings);
    if (res < 0) {
        return false;
    }
    return true;
}

bool config_gnss(int gnss_fd) {
    std::vector<std::uint8_t> payload;
    payload.reserve(CFG_PAYLOAD_SIZE);

    appendU32LE(payload, CFG_VALSET_HEADER);
    appendU32LE(payload, CFG_TMODE_MODE);
    payload.push_back(CFG_TMODE_MODE_ROV);
    appendU32LE(payload, CFG_USBINPROT_UBX);
    payload.push_back(CFG_USBINPROT_UBX_EN);
    appendU32LE(payload, CFG_USBINPROT_RTCM3X);
    payload.push_back(CFG_USBINPROT_RTCM3X_EN);
    appendU32LE(payload, CFG_USBOUTPROT_UBX);
    payload.push_back(CFG_USBOUTPROT_UBX_EN);
    appendU32LE(payload, CFG_MSGOUT_UBX_NAV_PVT_USB);
    payload.push_back(CFG_MSGOUT_UBX_NAV_PVT_USB_EN);
    appendU32LE(payload, CFG_MSGOUT_UBX_RXM_RTCM_USB);
    payload.push_back(CFG_MSGOUT_UBX_RXM_RTCM_USB_EN);
    uint16_t payload_length = static_cast<uint16_t>(payload.size());

    std::vector<std::uint8_t> cfg_valset_msg;
    cfg_valset_msg.reserve(payload.size() + 8);

    cfg_valset_msg.push_back(UBX_BYTE_1);
    cfg_valset_msg.push_back(UBX_BYTE_2);
    cfg_valset_msg.push_back((UBX_CFG_VALSET >> 8) & 0xFF);
    cfg_valset_msg.push_back(UBX_CFG_VALSET & 0xFF);
    cfg_valset_msg.push_back(payload_length & 0xFF);
    cfg_valset_msg.push_back((payload_length >> 8) & 0xFF);
    cfg_valset_msg.insert(cfg_valset_msg.end(), payload.begin(), payload.end());
    uint8_t ck_a = 0;
    uint8_t ck_b = 0;
    for (size_t i = 2; i < cfg_valset_msg.size(); i++) {
        ck_a += cfg_valset_msg[i];
        ck_b += ck_a;
    }
    cfg_valset_msg.push_back(ck_a);
    cfg_valset_msg.push_back(ck_b);

    size_t msg_size = cfg_valset_msg.size();
    size_t bytes_written = 0;
    while (bytes_written < msg_size) {
        ssize_t res =
            write(gnss_fd, cfg_valset_msg.data() + bytes_written, msg_size - bytes_written);
        if (res < 0) {
            if (errno == EINTR) {
                continue; // Interrupted by signal, retry writing
            }
            perror("write(config_gnss)");
            return false;
        } else if (res == 0) {
            perror("write(config_gnss): wrote 0 bytes");
            return false; // No more bytes can be written
        }
        bytes_written += res;
    }
    return true;
}

// static readSerialData();

// processReceiveBuffer();

void writeRTKCorrections(int gnss_fd, std::vector<std::uint8_t> rtk_corrections) {
    int bytes_written = 0;
    while (bytes_written < rtk_corrections.size()) {
        int res = write(
            gnss_fd, rtk_corrections.data() + bytes_written, rtk_corrections.size() - bytes_written
        );
        if (res < 0) {
            if (errno == EINTR) {
                continue; // Interrupted by signal, retry writing
            }
            perror("write(rtk_corrections)");
            return;
        } else if (res == 0) {
            perror("write(rtk_corrections): wrote 0 bytes");
            return; // No more bytes can be written
        }
        bytes_written += res;
    }
}

bool tryExtractUbxFrame(
    boost::circular_buffer<std::uint8_t> &data_buf, std::vector<std::uint8_t> &ubx_frame
) {
    for (;;) {
        // Go through buf until UBX frame start is found or buf is exhausted
        while (data_buf.size() >= 2 && (data_buf[0] != UBX_BYTE_1 || data_buf[1] != UBX_BYTE_2)) {
            data_buf.erase_begin(1); // Remove the first byte and continue searching
        }

        // Not enough space for packet
        if (data_buf.size() < CIRC_BUF_OVERHEAD_SIZE) {
            return false;
        }

        // Try to extract UBX frame if start bytes are found
        uint16_t data_len = data_buf[4] | data_buf[5] << 8;   // Length is little-endian
        size_t frame_len = data_len + CIRC_BUF_OVERHEAD_SIZE; // Total length of the UBX frame
        if (frame_len > data_buf.capacity()) {
            data_buf.erase_begin(1);
            continue;
        }
        if (data_buf.size() < frame_len) {
            fprintf(stderr, "Incomplete frame");
            return false;
        }

        std::vector<std::uint8_t> frame(data_buf.begin(), data_buf.begin() + frame_len);

        if (!validateUbxChecksum(frame, data_len)) {
            data_buf.erase_begin(1);
            fprintf(stderr, "UBX checksum not valid");
            continue;
        }

        ubx_frame = std::move(frame);
        data_buf.erase_begin(frame_len);
        return true;
    }
}

// MARK: Msg Handlrs
// Parses out UBX msg type and returns this for gnss thread to handle.
// Returns -1 for unhandled msg, 0 for ack/nak, or UBX msg type if it needs to be handled.
uint16_t handleUbxMessage(std::vector<std::uint8_t> &ubx_frame) {
    uint8_t msg_class = ubx_frame[2];
    uint8_t msg_id = ubx_frame[3];
    if (uint16_t(msg_class << 8 | msg_id) == UBX_NAV_PVT) {
        return UBX_NAV_PVT;
    } else if (uint16_t(msg_class << 8 | msg_id) == UBX_RXM_RTCM) {
        return UBX_RXM_RTCM;
    } else if (
        uint16_t(msg_class << 8 | msg_id) == UBX_ACK_ACK ||
        uint16_t(msg_class << 8 | msg_id) == UBX_ACK_NAK
    ) {
        handleAck(ubx_frame);
        return 0;
    } else {
        std::printf("Received unhandled UBX message: Class: %02X, ID: %02X\n", msg_class, msg_id);
        return -1;
    }
}

// MARK: NAV PVT
bool handleNavPvt(const std::vector<std::uint8_t> &ubx_frame, pi_to_server &pkt) {
    if (ubx_frame.size() < UBX_FRAME_OVERHEAD) {
        return false;
    }

    const std::uint16_t payload_length = readUInt16LE(ubx_frame.data() + UBX_PAYLOAD_LENGTH_OFFSET);

    // NAV-PVT has a 92-byte payload. The input is a complete UBX frame.
    if (payload_length != NAV_PVT_PAYLOAD_SIZE ||
        ubx_frame.size() != UBX_FRAME_OVERHEAD + payload_length) {
        std::fprintf(stderr, "Invalid NAV-PVT frame length\n");
        return false;
    }

    bool ret = true;

    const std::uint8_t *payload = ubx_frame.data() + UBX_HEADER_SIZE;

    const float longitude = readInt32LE(payload + PVT_LONGITUDE_OFFSET) * PVT_COORDINATE_SCALE;
    const float latitude = readInt32LE(payload + PVT_LATITUDE_OFFSET) * PVT_COORDINATE_SCALE;
    const double elevation =
        readInt32LE(payload + PVT_HEIGHT_MSL_OFFSET) * PVT_MILLIMETERS_TO_METERS;
    const double horizontal_accuracy =
        readUInt32LE(payload + PVT_HORIZONTAL_ACCURACY_OFFSET) * PVT_MILLIMETERS_TO_METERS;
    const double speed = readInt32LE(payload + PVT_GROUND_SPEED_OFFSET) * PVT_MILLIMETERS_TO_METERS;
    const double heading = readInt32LE(payload + PVT_HEADING_OFFSET) * PVT_HEADING_SCALE;

    const std::uint8_t flags = payload[PVT_FLAGS_OFFSET];
    const std::uint8_t fix_type = payload[PVT_FIX_TYPE_OFFSET];
    const std::uint8_t satellites = payload[PVT_NUM_SATELLITES_OFFSET];

    const bool gnss_fix_ok = (flags & PVT_GNSS_FIX_OK_MASK) != 0;
    const bool differential_solution = (flags & PVT_DIFFERENTIAL_SOLUTION_MASK) != 0;
    const bool position_valid = (payload[PVT_FLAGS3_OFFSET] & PVT_INVALID_LLH_MASK) == 0;

    const auto carrier_solution = static_cast<PVT_CarrierSolution>(
        (flags & PVT_CARRIER_SOLUTION_MASK) >> PVT_CARRIER_SOLUTION_SHIFT
    );

    const char *rtk_status = "Unknown";
    switch (carrier_solution) {
    case PVT_CarrierSolution::NO_RTK:
        rtk_status = "No RTK";
        break;
    case PVT_CarrierSolution::RTK_FLOAT:
        rtk_status = "RTK float";
        break;
    case PVT_CarrierSolution::RTK_FIXED:
        rtk_status = "RTK fixed";
        break;
    default:
        break;
    }

    auto fill_pkt = [&]() {
        const float lat = static_cast<float>(latitude);
        const float lon = static_cast<float>(longitude);
        std::memcpy(pkt.bytes, &lat, sizeof(lat));
        std::memcpy(pkt.bytes + 4, &lon, sizeof(lon));
        pkt.length = 8;
    };

    std::printf(
        "%s | DiffCorrections: %s | FixOK: %s | Fix Type: %u | NumSat: %u\n", rtk_status,
        differential_solution ? "true" : "false", gnss_fix_ok ? "true" : "false",
        static_cast<unsigned>(fix_type), static_cast<unsigned>(satellites)
    );

    if (position_valid && gnss_fix_ok) {
        std::printf(
            "Lat: %.7f | Lon: %.7f | Acc: %.3f m | Elev: %.3f m\n", latitude, longitude,
            horizontal_accuracy, elevation
        );

        // Setting values for GPS pkt
        fill_pkt();
    } else {
        std::printf("Pos Invalid\n");
        ret = false;
    }

    std::printf("Speed: %.3f m/s | Heading: %.5f deg\n\n", speed, heading);

    return ret;
}

void handleAck(std::vector<std::uint8_t> &ubx_frame) {}

bool handleRxmRtcm(std::vector<std::uint8_t> &ubx_frame) {
    // Need to check if the message is used, get type, and check msg.crcFailed.
    std::printf("Received UBX-RXM-RTCM message of length: %zu\n", ubx_frame.size());

    // First, check if the message is used.
    // First, actually, let's get the flags byte out.

    uint8_t flags = ubx_frame[7];
    // shift right to get rid of crcFailed bit, initially the LSB.
    // Now, check if the message is used.
    const uint8_t crcFailed = flags & 0x01;      // If zero, passed CRC check, if 1, failed
    const uint8_t msgUsed = (flags >> 1) & 0x03; // 0 = unknown, 1 = unused, 2 = used

    // Checking for crcFailed first.
    if (crcFailed) { // Fails when true
        std::cout << "UBX-RXM-RTCM message failed CRC check" << std::endl;
        return false;
    }

    // Checking for msgUsed state.
    if (msgUsed == 0) {
        std::cout << "UBX-RXM-RTCM msgUsed in unknown state" << std::endl;
        return false;
    } else if (msgUsed == 1) {
        std::cout << "UBX-RXM-RTCM msgUsed in unused state" << std::endl;
        return false;
    } else if (msgUsed == 2) {
        std::cout << "UBX-RXM-RTCM msgUsed in used state" << std::endl;
    }
    return false;
}
