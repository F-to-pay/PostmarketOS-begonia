#!/bin/sh
# Powers the phone off after IDLE_MIN minutes without input (touch/buttons). Runs as root, without a DE.
IDLE_MIN=${IDLE_MIN:-60}
GRACE=${GRACE:-60}      # final window: any touch cancels
POLL=${POLL:-20}
# Only touch and buttons, so that the sensors do not create phantom activity.
# Override: INPUT_DEVICES="/dev/input/event1 /dev/input/event2"
NAME_RE=${NAME_RE:-'touch|ts$|key|button|power|volume|fts|goodix|himax|nvt|spi'}

pick_devices() {
    [ -n "$INPUT_DEVICES" ] && { echo "$INPUT_DEVICES"; return; }
    for n in /sys/class/input/event*; do
        name=$(cat "$n/device/name" 2>/dev/null)
        echo "$name" | grep -qiE "$NAME_RE" && echo "/dev/input/${n##*/}"
    done
}

# Listens to the devices for $1 seconds, prints the number of bytes received (0 = silence).
listen() {
    (
        for d in $DEVS; do cat "$d" & done
        sleep "$1"
        kill $(jobs -p) 2>/dev/null
    ) | wc -c
}

DEVS=$(pick_devices)
[ -z "$DEVS" ] && { echo "no input devices, exiting" >&2; exit 1; }
echo "watching: $DEVS" >&2

limit=$(( IDLE_MIN * 60 ))
last=$(date +%s)    # wall clock, not a sleep counter: survives suspend
while true; do
    if [ "$(listen "$POLL")" -gt 0 ]; then
        last=$(date +%s)
    elif [ $(( $(date +%s) - last )) -ge "$limit" ]; then
        if [ "$(listen "$GRACE")" -gt 0 ]; then
            last=$(date +%s)
        else
            poweroff
            sleep 60
        fi
    fi
done
