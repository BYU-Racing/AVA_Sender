#ifndef CAD521CE_2B04_42DE_95E2_6A23FD2563DC
#define CAD521CE_2B04_42DE_95E2_6A23FD2563DC

#include <boost/circular_buffer.hpp>
#include <cstdint>
#include <vector>

// MARK: MSG IDs
// UBX message IDs, Class << 8 | ID
constexpr uint16_t UBX_NAV_PVT = 0x0107;
constexpr uint16_t UBX_RXM_RTCM = 0x0232;
constexpr uint16_t UBX_ACK_ACK = 0x0501;
constexpr uint16_t UBX_ACK_NAK = 0x0500;
constexpr uint16_t UBX_CFG_VALSET = 0x068A;

// MARK: GNSS CFG
// RAM-only config
constexpr uint32_t CFG_VALSET_HEADER = 0x00000100; // RAM only, no save to flash
// Key IDs for CFG_VALSET
constexpr uint32_t CFG_TMODE_MODE = 0x20030001;
constexpr uint32_t CFG_USBINPROT_UBX = 0x10770001;
constexpr uint32_t CFG_USBINPROT_RTCM3X = 0x10770004;
constexpr uint32_t CFG_USBOUTPROT_UBX = 0x10780001;
constexpr uint32_t CFG_MSGOUT_UBX_NAV_PVT_USB = 0x20910009;
constexpr uint32_t CFG_MSGOUT_UBX_RXM_RTCM_USB = 0x2091026B;
// Vals for each Key ID
constexpr uint8_t CFG_TMODE_MODE_ROV = 0x00;
constexpr uint8_t CFG_USBINPROT_UBX_EN = 0x01;
constexpr uint8_t CFG_USBINPROT_RTCM3X_EN = 0x01;
constexpr uint8_t CFG_USBOUTPROT_UBX_EN = 0x01;
constexpr uint8_t CFG_MSGOUT_UBX_NAV_PVT_USB_EN = 0x01;
constexpr uint8_t CFG_MSGOUT_UBX_RXM_RTCM_USB_EN = 0x01;

// MARK: Func Decs
// ======== Helper funcs ========

// Puts each byte of 32-bit value into vec
static void appendU32LE(std::vector<std::uint8_t> &vec, std::uint32_t value);
// Validates that checksum matches data in frame. Returns true if valid.
static bool validateUbxChecksum(std::vector<std::uint8_t> frame, std::uint16_t length);

// ======== User funcs ========

bool setupGnssFd(int gnss_fd); // Prepares the GNSS file desc for rd/wr. Return true on success.
bool config_gnss(int gnss_fd); // Writes configs to GNSS device. Return true on success.

// Writes RTK corrections from Base Station to the GNSS device. Returns true on success.
void writeRTKCorrections(int gnss_fd, std::vector<std::uint8_t> rtk_corrections);
// Tries to extract a UBX frame from data_buf into ubx_frame. Returns true if a frame was extracted.
bool tryExtractUbxFrame(
    boost::circular_buffer<std::uint8_t> &data_buf, std::vector<std::uint8_t> &ubx_frame
);

void handleUbxMessage(std::vector<std::uint8_t> &ubx_frame); // Routes UBX msgs to spec handlers.
void handleAck(std::vector<std::uint8_t> &ubx_frame);        // Handler for UBX-ACK messages.
void handleNavPvt(std::vector<std::uint8_t> &ubx_frame);     // Handler for UBX-NAV-PVT messages.
bool handleRxmRtcm(std::vector<std::uint8_t> &ubx_frame);    // Handler for UBX-RXM-RTCM messages.

#endif /* CAD521CE_2B04_42DE_95E2_6A23FD2563DC */
