
#!/usr/bin/env bash

PORT="/dev/ttyUSB2"
INTERVAL=10
LOGFILE="lte_log_$(date +%Y%m%d_%H%M%S).txt"

# Configure serial port
stty -F "$PORT" 115200 raw -echo || exit 1

# Open serial port for reading and writing
exec 3<>"$PORT" || exit 1

send_at() {
    local command="$1"
    local line
    local response=""

    printf '%s\r' "$command" >&3

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

    printf '%s\n' "$response"
}

# Display and log simultaneously
log() {
    echo "$1" | tee -a "$LOGFILE"
}

PREVIOUS_BAND=""

log "LTE Monitoring Started: $(date)"
log "Log file: $LOGFILE"

while true; do
    CPSI=$(send_at "AT+CPSI?")
    CSQ=$(send_at "AT+CSQ")
    CESQ=$(send_at "AT+CESQ")

    # Remove response prefixes
    CPSI="${CPSI#*+CPSI: }"
    CSQ="${CSQ#*+CSQ: }"
    CESQ="${CESQ#*+CESQ: }"

    # Parse CPSI response
    IFS=',' read -r MODE STATUS MCCMNC CELL_ID PCI BAND EARFCN REST <<< "$CPSI"

    # Parse CSQ
    IFS=',' read -r CSQ_VALUE BER <<< "$CSQ"

    # Parse CESQ
    IFS=',' read -r RXLEV BER2 RSCP ECNO RSRQ RSRP <<< "$CESQ"

    # Convert signal measurements
    if [[ "$CSQ_VALUE" =~ ^[0-9]+$ ]] && (( CSQ_VALUE <= 31 )); then
        RSSI=$((-113 + 2 * CSQ_VALUE))
        RSSI="${RSSI} dBm"
    else
        RSSI="Unknown"
    fi

    if [[ "$RSRP" =~ ^[0-9]+$ ]] && (( RSRP <= 97 )); then
        RSRP_DBM=$((-140 + RSRP))
        RSRP_TEXT="${RSRP_DBM} dBm"
    else
        RSRP_TEXT="Unknown"
    fi

    if [[ "$RSRQ" =~ ^[0-9]+$ ]] && (( RSRQ <= 34 )); then
        RSRQ_TEXT=$(awk -v v="$RSRQ" 'BEGIN {printf "%.1f dB", -19.5 + v * 0.5}')
    else
        RSRQ_TEXT="Unknown"
    fi

    TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')

    log "----------------------------------------"
    log "Time:       $TIMESTAMP"
    log "Network:    $MODE ($STATUS)"
    log "Band:       $BAND"
    log "Cell ID:    $CELL_ID"
    log "PCI:        $PCI"
    log "EARFCN:     $EARFCN"
    log "CSQ:        $CSQ_VALUE"
    log "RSSI:       $RSSI"
    log "RSRP:       $RSRP_TEXT"
    log "RSRQ:       $RSRQ_TEXT"

    if [[ -n "$PREVIOUS_BAND" && "$BAND" != "$PREVIOUS_BAND" ]]; then
        log "*** BAND CHANGED: $PREVIOUS_BAND -> $BAND ***"
    fi

    PREVIOUS_BAND="$BAND"

    sleep "$INTERVAL"
done
