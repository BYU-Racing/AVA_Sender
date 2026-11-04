#!/bin/bash

PORT = "/dev/ttyUSB2"
LOG = "lte_log.csv"

# Configure serial port
stty -F "$PORT" 115200 raw -echo

# Create CSV header
if [ ! -f "$LOG" ]; then
    echo "Timestamp,CPSI,CSQ" > "$LOG"
fi

# Open serial port for reading and writing
exec 3<>"$PORT"

# Read modem responses
send_at() {
    local command="$1"
    local response=""

    # Send AT command
    printf '%s\r' "$command" >&3

    # Read until OK or ERROR
    while IFS= read -r -t 3 line <&3; do
        line="${line//$'\r'/}"
        [[ -z "$line" ]] && continue

        if [[ "$line" == "OK" || "$line" == "ERROR" ]]; then
            break
        fi

        if [[ "$line" == +* ]]; then
            response="$line"
        fi
    done

    echo "$response"
}

while true; do
    TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')

    CPSI=$(send_at "AT+CPSI?")
    CSQ=$(send_at "AT+CSQ")

    echo "$TIMESTAMP | $CPSI | $CSQ"

    # Escape quotes for CSV
    CPSI="${CPSI//\"/\"\"}"
    CSQ="${CSQ//\"/\"\"}"

    echo "\"$TIMESTAMP\",\"$CPSI\",\"$CSQ\"" >> "$LOG"

    sleep 10
done
