#include <cerrno>
#include <cstdio>

#include <termios.h>
#include <unistd.h>

#include "ublox_gnss.h"

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
    // Check that buf is not empty and has min UBX frame size (8 bytes)
    if (data_buf.empty() || data_buf.size() < CIRC_BUF_OVERHEAD) {
        return false;
    }
    // Go through buf until UBX frame start is found or buf is exhausted
    while (data_buf.size() >= CIRC_BUF_OVERHEAD) {
        if (data_buf[0] == UBX_BYTE_1 && data_buf[1] == UBX_BYTE_2) {
            break; // Found potential UBX frame start
        } else {
            data_buf.erase_begin(1); // Remove the first byte and continue searching
        }
    }
    // Try to extract UBX frame if start bytes are found
    if ((data_buf[0] == UBX_BYTE_1) && (data_buf[1] == UBX_BYTE_2)) {
        uint16_t data_len = data_buf[4] | data_buf[5] << 8; // Length is little-endian
        size_t frame_len = data_len + CIRC_BUF_OVERHEAD;    // Total length of the UBX frame
        if (data_buf.size() < frame_len) {
            return false;
        } else {
            std::vector<std::uint8_t> frame(data_buf.begin(), data_buf.begin() + frame_len);
            if (!validateUbxChecksum(frame, data_len)) {
                data_buf.erase_begin(1);
                perror("Invalid UBX checksum");
                return false;
            }
            if (data_buf.size() < frame_len) {
                perror("Not enough data for full UBX frame");
                return false;
            }
            ubx_frame = frame;
            data_buf.erase_begin(frame_len);
            return true;
        }
    }
    return false;
}

void handleUbxMessage(std::vector<std::uint8_t> &ubx_frame) {
    uint8_t msg_class = ubx_frame[2];
    uint8_t msg_id = ubx_frame[3];
    if (uint16_t(msg_class << 8 | msg_id) == UBX_NAV_PVT) {
        handleNavPvt(ubx_frame);
    } else if (uint16_t(msg_class << 8 | msg_id) == UBX_RXM_RTCM) {
        handleRxmRtcm(ubx_frame);
    } else if (
        uint16_t(msg_class << 8 | msg_id) == UBX_ACK_ACK ||
        uint16_t(msg_class << 8 | msg_id) == UBX_ACK_NAK
    ) {
        handleAck(ubx_frame);
    } else {
        std::printf("Received unhandled UBX message: Class: %02X, ID: %02X\n", msg_class, msg_id);
    }
}

// handleNavPvt();

void handleAck(std::vector<std::uint8_t> &ubx_frame) {}

bool handleRxmRtcm(std::vector<std::uint8_t> &ubx_frame) {
    // Need to check if the message is used, get type, and check msg.crcFailed.
    std::printf("Received UBX-RXM-RTCM message of length: %uz\n", ubx_frame.size());
    return true;
}
