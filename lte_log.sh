
#!/bin/bash

PORT="/dev/ttyUSB2"

# Configure serial port
stty -F "$PORT" 115200 raw -echo

# Open serial port for reading and writing
exec 3<>"$PORT"

send_at() {
    local command="$1"

    printf '%s\r' "$command" >&3

    while IFS= read -r -t 3 line <&3; do
        line="${line//$'\r'/}"
        [[ -z "$line" ]] && continue

        echo "$line"

        if [[ "$line" == "OK" || "$line" == "ERROR" ]]; then
            break
        fi
    done
}

while true; do
    echo "----------------------------------------"
    echo "Time: $(date '+%Y-%m-%d %H:%M:%S')"

    send_at "AT+CPSI?"
    send_at "AT+CSQ"

    sleep 10
done
