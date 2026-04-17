import asyncio
import random
import struct
import time
import websockets

WS_URL = "ws://100.85.246.127:8000/api/ws/send"

# struct pi_to_server:
# uint32_t timestamp; uint32_t id; uint8_t length; uint8_t bytes[8];
# Packed, little-endian -> "<I I B 8s" = 17 bytes
FMT = "<I I B 8s"
assert struct.calcsize(FMT) == 17

DBC_MESSAGE_IDS = [
    0, 1, 2, 3, 4, 5, 6, 9,
    160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 176, 177,
    192, 193, 194,
]


def now_ms() -> int:
    return int(time.monotonic() * 1000) & 0xFFFFFFFF


def i16(value: int) -> bytes:
    return int(value).to_bytes(2, "little", signed=True)


def u16(value: int) -> bytes:
    return int(value).to_bytes(2, "little", signed=False)


def i32(value: int) -> bytes:
    return int(value).to_bytes(4, "little", signed=True)


def u32(value: int) -> bytes:
    return int(value).to_bytes(4, "little", signed=False)


def f32(value: float) -> bytes:
    return struct.pack("<f", value)


def pack_int16s(*values: int) -> bytearray:
    data = bytearray(8)
    for index, value in enumerate(values):
        start = index * 2
        data[start:start + 2] = i16(value)
    return data


def set_bits(data: bytearray, start_bit: int, bit_length: int, value: int) -> None:
    mask = (1 << bit_length) - 1
    payload = int.from_bytes(data, "little")
    payload &= ~(mask << start_bit)
    payload |= (int(value) & mask) << start_bit
    data[:] = payload.to_bytes(len(data), "little")


def make_packet(msg_id=0) -> bytes:
    ts = now_ms()
    length, data = make_can_payload(msg_id)
    return struct.pack(FMT, ts, msg_id, length, bytes(data))


def make_can_payload(msg_id: int) -> tuple[int, bytearray]:
    data = bytearray(8)

    match msg_id:
        case 0:  # StartSwitch: 1 byte
            data[0] = random.randint(0, 1)
            return 1, data

        case 1 | 2:  # Throttle position: uint16
            data[0:2] = u16(random.randint(0, 65535))
            return 2, data

        case 3:  # Brake pressure: uint16
            data[0:2] = u16(random.randint(0, 65535))
            return 2, data

        case 4:  # RVC multiplexed float32
            mux = random.randint(0, 5)
            values = {
                0: random.uniform(-3.0, 3.0),      # X acceleration
                1: random.uniform(-3.0, 3.0),      # Y acceleration
                2: random.uniform(-3.0, 3.0),      # Z acceleration
                3: random.uniform(-180.0, 180.0),  # X rotation
                4: random.uniform(-180.0, 180.0),  # Y rotation
                5: random.uniform(-180.0, 180.0),  # Z rotation
            }
            data[0] = mux
            data[1:5] = f32(values[mux])
            return 5, data

        case 5:  # TireRPM multiplexed float32
            mux = random.randint(0, 3)
            data[0] = mux
            data[1:5] = f32(random.uniform(0.0, 2200.0))
            return 5, data

        case 6:  # TireTemperature multiplexed int16 in/out/mid
            mux = random.randint(0, 3)
            data[0] = mux
            data[1:3] = i16(random.randint(20, 100))
            data[3:5] = i16(random.randint(20, 100))
            data[5:7] = i16(random.randint(20, 100))
            return 7, data

        case 9:  # GPS: longitude first, then latitude per DBC
            lat_e7 = int((42 + random.uniform(-0.05, 0.05)) * 1e7)
            lon_e7 = int((-105 + random.uniform(-0.05, 0.05)) * 1e7)
            data[0:4] = i32(lon_e7)
            data[4:8] = i32(lat_e7)
            return 8, data

        case 160:  # InverterTemps
            return 8, pack_int16s(
                random.randint(25, 90),
                random.randint(25, 90),
                random.randint(25, 90),
                random.randint(25, 90),
            )

        case 161:  # ControlBoardTemp
            return 8, pack_int16s(
                random.randint(25, 75),
                random.randint(25, 75),
                random.randint(25, 75),
                random.randint(25, 75),
            )

        case 162:  # MotorTemps
            return 8, pack_int16s(
                random.randint(25, 80),
                random.randint(25, 110),
                random.randint(25, 95),
                random.randint(0, 20),
            )

        case 163:  # AnalogInputVoltages
            return 8, pack_int16s(
                random.randint(0, 5000),
                random.randint(0, 5000),
                random.randint(0, 5000),
                random.randint(0, 5000),
            )

        case 164:  # DigitalInputStatus
            data[0:8] = bytes(random.randint(0, 1) for _ in range(8))
            return 8, data

        case 165:  # MotorPositionInfo
            return 8, pack_int16s(
                random.randint(0, 3600),
                random.randint(-5000, 5000),
                random.randint(0, 500),
                random.randint(-1800, 1800),
            )

        case 166:  # CurrentInfo
            return 8, pack_int16s(
                random.randint(-3000, 3000),
                random.randint(-3000, 3000),
                random.randint(-3000, 3000),
                random.randint(-1000, 1000),
            )

        case 167:  # VoltageInfo
            return 8, pack_int16s(
                random.randint(2500, 4200),
                random.randint(0, 4200),
                random.randint(0, 4200),
                random.randint(0, 4200),
            )

        case 168:  # FluxInfo
            return 8, pack_int16s(
                random.randint(-1000, 1000),
                random.randint(-1000, 1000),
                random.randint(-1000, 1000),
                random.randint(-1000, 1000),
            )

        case 169:  # InternalVoltages
            return 8, pack_int16s(150, 250, 500, 1200)

        case 170:  # InternalStates
            data[0] = random.choice([0, 1, 2, 3, 4, 5, 6, 14])
            data[1] = random.choice([10, 12, 16, 20])
            data[2] = random.choice([0, 1, 2, 3, 4, 8, 9])
            data[3] = random.randint(0, 5)
            set_bits(data, 32, 1, random.randint(0, 1))  # InverterRunModeState
            set_bits(data, 33, 1, random.randint(0, 1))  # SelfSensingAssistActive
            set_bits(data, 37, 3, random.randint(0, 4))  # InverterDischarge
            set_bits(data, 40, 1, random.randint(0, 1))  # InverterCommandMode
            set_bits(data, 44, 4, random.randint(0, 15)) # InverterRollingCounter
            return 8, data

        case 171:  # FaultCodes
            return 8, bytearray(
                u16(random.choice([0, 0, 0, 1, 2, 4])) +
                u16(random.choice([0, 0, 0, 1, 2, 4])) +
                u16(random.choice([0, 0, 0, 1, 2, 4])) +
                u16(random.choice([0, 0, 0, 1, 2, 4]))
            )

        case 172:  # TorqueAndTimerInfo
            data[0:2] = i16(random.randint(-1500, 1500))
            data[2:4] = i16(random.randint(-1500, 1500))
            data[4:8] = u32((now_ms() // 3) & 0xFFFFFFFF)
            return 8, data

        case 173:  # ModulationIndex
            return 8, pack_int16s(
                random.randint(0, 100),
                random.randint(-500, 500),
                random.randint(-500, 500),
                random.randint(-500, 500),
            )

        case 176:  # HighSpeed
            return 8, pack_int16s(
                random.randint(-1500, 1500),
                random.randint(-1500, 1500),
                random.randint(-5000, 5000),
                random.randint(2500, 4200),
            )

        case 177:  # TorqueCapability
            data[0:2] = i16(random.randint(0, 3000))
            return 8, data

        case 192:  # ControlCommand
            data[0:2] = i16(random.randint(-1500, 1500))
            data[2:4] = i16(random.randint(0, 5000))
            data[4] = random.randint(0, 1)
            set_bits(data, 40, 1, random.randint(0, 1))  # EnableInverter
            set_bits(data, 41, 1, random.randint(0, 1))  # Discharge
            set_bits(data, 42, 1, random.randint(0, 1))  # OverrideSpeed
            data[6:8] = i16(random.randint(0, 3000))
            return 8, data

        case 193:  # ParameterCommand
            data[0:2] = u16(random.randint(0, 65535))
            data[2] = random.randint(0, 1)
            data[4:6] = u16(random.randint(0, 65535))
            return 8, data

        case 194:  # ParameterResponse
            data[0:2] = u16(random.randint(0, 65535))
            data[2] = random.randint(0, 1)
            data[4:6] = u16(random.randint(0, 65535))
            return 8, data

        case _:
            for index in range(8):
                data[index] = random.randint(0, 255)
            return 8, data

async def main():
    msg_index = 0
    print(f"Connecting to {WS_URL}")
    async with websockets.connect(WS_URL, ping_interval=20, ping_timeout=20) as ws:
        print(f"Connected. Sending fake packets for {len(DBC_MESSAGE_IDS)} DBC IDs (Ctrl+C to stop).")
        while True:
            msg_id = DBC_MESSAGE_IDS[msg_index]
            pkt = make_packet(msg_id)
            await ws.send(pkt)  # sends as binary frame
            await asyncio.sleep(0.002)  # in sec
            msg_index = (msg_index + 1) % len(DBC_MESSAGE_IDS)

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nStopped.")
