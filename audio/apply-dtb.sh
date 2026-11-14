#!/bin/sh
# Rebuilds begonia-audio.dtb (with the new ADDA clocks) and installs it into /boot. A reboot is needed afterwards.
# Run: sh /home/user/Begonia/wifi/audio/apply-dtb.sh   (no sudo before sh; install-audio.sh will ask for the password)
# Rollback: sh /home/user/Begonia/wifi/audio/rollback-audio.sh
set -e
H=/home/user/Begonia/wifi/audio
echo "== 1/2 building the DTB =="
sh "$H/make-audio-dtb.sh"
echo "== check: new clocks in the built DTB =="
dtc -q -I dtb -O dts /home/user/begonia-audio.dtb 2>/dev/null | grep -c 'aud_adda6_adc_clk' | sed 's/^/mentions of aud_adda6_adc_clk: /'
echo "== 2/2 installing into /boot =="
sh "$H/install-audio.sh"
echo
echo "DONE. Reboot, then: sh $H/load-audio-safe.sh"
