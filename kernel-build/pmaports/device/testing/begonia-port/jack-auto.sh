#!/bin/sh
# 3.5 mm jack automation (ACCDET, jack control "Headphone Jack" of the mt6785_mt6359 card).
#   headphones plugged in -> hp-on (headphone output on) + spk-off (speaker and its filter unloaded)
#   pulled out            -> hp-off (output muted). (see below about the speaker)
#                        Default: pulled out -> speaker (with the bass filter, volume JACK_SPK_VOL, default 40%).
#                        Silence only, no speaker: JACK_UNPLUG=mute.
# Event driven: `alsactl monitor` blocks until a control of the card changes (the jack control notifies on plug/unplug),
# so the script costs nothing while the jack does not move (it used to poll amixer 3 times a second).
# The contact bounces on insertion/removal, so the state is read ~0.9 s after the event and acted upon only if it changed.
# Installed as /usr/bin/begonia-jack-auto and run by the user service begonia-jack.service
# (hp-on/hp-off go to amixer, spk-* to the user PulseAudio).
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

last=-1
check() {
	sleep 0.9			# contact bounce
	s=$(jack_state)
	if [ "$s" != "$last" ]; then
		last=$s
		apply "$s"
	fi
}

check				# the state at the start
while :; do
	# one line per notification of any control of the card; only the jack ones matter
	alsactl monitor hw:0 2>/dev/null | while read -r line; do
		case "$line" in
		*"Headphone Jack"*) check ;;
		esac
	done
	sleep 3			# the card is not there (yet) / alsactl exited: try again
done
