#!/bin/sh
# 3.5 mm jack automation (ACCDET, jack control "Headphone Jack" of the mt6785_mt6359 card).
#   headphones plugged in -> hp-on (headphone output on) + spk-off (speaker and its filter unloaded)
#   pulled out            -> hp-off (output muted). (see below about the speaker)
#                        Default: pulled out -> speaker (with the bass filter, volume JACK_SPK_VOL, default 40%).
#                        Silence only, no speaker: JACK_UNPLUG=mute.
# The contact bounces on insertion/removal, so only a state unchanged for ~0.9 s is acted upon.
# Runs as a systemd user service (begonia-jack.service): hp-on/hp-off go to amixer, spk-* to the user PulseAudio.
UNPLUG=${JACK_UNPLUG:-speaker}
SPKVOL=${JACK_SPK_VOL:-40}
LOG=/tmp/jack-auto.log

jack_state() {
	amixer -c 0 cget iface=CARD,name='Headphone Jack' 2>/dev/null | tail -1 | grep -q 'values=on' && echo 1 || echo 0
}

log() { echo "$(date +%H:%M:%S) $*" >> "$LOG"; logger -t begonia-jack "$*" 2>/dev/null; }

apply() {
	if [ "$1" = 1 ]; then
		log "headphones plugged in: hp-on, speaker off"
		hp-on 1 >/dev/null 2>&1
		spk-off >/dev/null 2>&1
	else
		log "headphones pulled out: hp-off"
		hp-off >/dev/null 2>&1
		if [ "$UNPLUG" = speaker ]; then
			log "JACK_UNPLUG=speaker: enabling the speaker ($SPKVOL%)"
			spk-on "$SPKVOL" >/dev/null 2>&1
		fi
	fi
}

last=-1; cand=-1; stable=0
while :; do
	s=$(jack_state)
	if [ "$s" = "$cand" ]; then
		stable=$((stable + 1))
	else
		cand=$s
		stable=0
	fi
	if [ "$stable" -ge 3 ] && [ "$cand" != "$last" ]; then
		last=$cand
		apply "$cand"
	fi
	sleep 0.3
done
