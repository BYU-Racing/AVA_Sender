from pyubx2 import UBXReader
from serial import Serial

port = "/dev/ttyACM0"
baud = 115200

stream = Serial(port, baud, timeout=1)
ubr = UBXReader(stream)

# print(f"Listening on {port} at {baud} baud...", flush=True)

while True:
    raw, msg = ubr.read()

    if msg is None:
        # print("No GNSS message received (read timeout)", flush=True)
        continue

    # print(msg, flush=True)

    # -----------------------------
    # Position / fix information
    # -----------------------------
    if msg.identity == "NAV-PVT":
        # print("\n===== GNSS =====")
        # print("Lat:", msg.lat)
        # print("Lon:", msg.lon)
        # print("Altitude:", msg.hMSL, "mm")
        # print("Satellites:", msg.numSV)
        print(
            f"Lat: {msg.lat} | Lon: {msg.lon} | Satellites: {msg.numSV}",
            flush=True,
        )

        # Position accuracy estimates
        # print("Horizontal accuracy:", msg.hAcc, "mm")
        # print("Vertical accuracy:", msg.vAcc, "mm")

        # Basic GNSS fix
        fix_names = {
            0: "No fix",
            1: "Dead reckoning",
            2: "2D",
            3: "3D",
            4: "GNSS + dead reckoning",
            5: "Time only",
        }

        # print("Fix:", fix_names.get(msg.fixType, "Unknown"))

        # RTK carrier solution: 0 = none, 1 = float, 2 = fixed
        rtk_names = {0: "None", 1: "RTK FLOAT", 2: "RTK FIXED"}
        # print("RTK:", rtk_names.get(msg.carrSoln, "Unknown"))

        # Simple integrity checks
        if msg.fixType < 3:
            # print("WARNING: No 3D GNSS fix")
            pass

        if msg.numSV < 6:
            # print("WARNING: Low satellite count")
            pass

        if msg.hAcc > 5000:
            # print("WARNING: Poor horizontal accuracy")
            pass

    # -----------------------------
    # Satellite signal quality
    # -----------------------------
    elif msg.identity == "NAV-SAT":
        cn0_values = []

        for i in range(msg.numSvs):
            cn0 = getattr(msg, f"cno_{i + 1:02d}", None)

            if cn0 is not None:
                cn0_values.append(cn0)

        if cn0_values:
            avg_cn0 = sum(cn0_values) / len(cn0_values)

            # print("\n===== SIGNAL =====")
            # print(f"Average C/N0: {avg_cn0:.1f} dB-Hz")
            # print(f"Strongest:    {max(cn0_values)} dB-Hz")
            # print(f"Weakest:      {min(cn0_values)} dB-Hz")

            if avg_cn0 >= 35:
                # print("Signal: GOOD")
                pass
            elif avg_cn0 >= 25:
                # print("Signal: FAIR")
                pass
            else:
                # print("Signal: POOR")
                pass
