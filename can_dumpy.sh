OUT_DIR="/home/ava3/AVA_Sender/Logs"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")

if [ ! -d "$OUT_DIR" ]; then
    mkdir -p "$OUT_DIR"
fi

candump can0 can1 > "$OUT_DIR/can_$TIMESTAMP.log" 2> "$OUT_DIR/can_$TIMESTAMP.err" &
