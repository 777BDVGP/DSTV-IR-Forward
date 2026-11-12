#!/usr/bin/env bash
# mov_monitor.sh — watch the "Surge protection (MOV)" state on TX 1, TX 2 and RX.
# Polls every 2 s (a false BLOWN lasts >= 3 s, so it can't slip through).
# Logs: start, every state change, unreachable/back, hourly "still OK" line.
# Usage: TX1_IP=... TX2_IP=... RX_IP=... bash tools/mov_monitor.sh [hours]   (default 24)
# Log:   IR_FORWARD/mov_monitor.log

LOG="$(cd "$(dirname "$0")/../.." && pwd)/mov_monitor.log"
HOURS="${1:-24}"
END=$(( $(date +%s) + HOURS * 3600 ))

declare -A last blowncount
names=(TX1 TX2 RX)

tx_state() {    # MOV text from a TX Home page script data ($1 = IP)
	curl -s -m 5 "http://$1/" | grep -o 'Surge protection (MOV).\{0,120\}' \
		| grep -o 'text: "[^"]*"' | head -1 | sed 's/text: "//; s/"$//'
}
: "${TX1_IP:?set TX1_IP}" "${TX2_IP:?set TX2_IP}" "${RX_IP:?set RX_IP}"
state_TX1() { tx_state "$TX1_IP"; }
state_TX2() { tx_state "$TX2_IP"; }
state_RX() {
	curl -s -m 5 "http://$RX_IP/api/status" | grep -o '"movText":"[^"]*"' | sed 's/.*:"//; s/"$//'
}

log() { echo "$(date '+%Y-%m-%d %H:%M:%S')  $*" | tee -a "$LOG"; }

log "=== MOV monitor started (TX1 .104, TX2 .105, RX .106), ${HOURS} h ==="
nexthour=$(( $(date +%s) + 3600 ))
while [ "$(date +%s)" -lt "$END" ]; do
	for n in "${names[@]}"; do
		s=$(state_$n); [ -z "$s" ] && s="UNREACHABLE"
		if [ "$s" != "${last[$n]}" ]; then
			case "$s" in BLOWN*) blowncount[$n]=$(( ${blowncount[$n]:-0} + 1 ));; esac
			log "$n: ${last[$n]:-start} -> $s"
			last[$n]="$s"
		fi
	done
	if [ "$(date +%s)" -ge "$nexthour" ]; then
		log "hourly: TX1=${last[TX1]} (BLOWN events ${blowncount[TX1]:-0})  TX2=${last[TX2]} (BLOWN events ${blowncount[TX2]:-0})  RX=${last[RX]} (BLOWN events ${blowncount[RX]:-0})"
		nexthour=$(( nexthour + 3600 ))
	fi
	sleep 2
done
log "=== MOV monitor finished: TX1 BLOWN events ${blowncount[TX1]:-0}, TX2 BLOWN events ${blowncount[TX2]:-0}, RX BLOWN events ${blowncount[RX]:-0} ==="
