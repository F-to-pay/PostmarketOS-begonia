#!/bin/sh
# Autoload of MT6785 audio (AFE + MT6359). Called from begonia-audio.service (root).
# Safe default: the headphone output is DISABLED (HP Mux=Open), minimum volume. Enable: hp-on [0-4]; disable: hp-off.
# Loop protection: if the previous run did not finish - autostart gets disabled (sudo rm /var/lib/begonia/audio-disabled).
# The modules must NOT be unloaded/reloaded without a reboot (one of the causes of hangs).
S=/var/lib/begonia
A=/home/user/Begonia/wifi/audio
mkdir -p "$S"
if [ -e "$S/audio-disabled" ]; then
	echo "begonia-audio: disabled by the protection ($S/audio-disabled). Enable: sudo rm $S/audio-disabled"; exit 0
fi
if [ -e "$S/audio-starting" ]; then
	echo "begonia-audio: the previous run did not finish -> disabling autostart"
	touch "$S/audio-disabled"; rm -f "$S/audio-starting"; sync; exit 0
fi
if lsmod | grep -q '^snd_soc_mt6785_afe'; then echo "begonia-audio: modules already loaded"; exit 0; fi
[ -d /proc/device-tree/soc/clock-controller@11210000/audio-controller ] || { echo "begonia-audio: no audio-controller in DT (begonia-audio.dtb needed)"; exit 1; }
touch "$S/audio-starting"; sync
# codecs/snd-soc-tfa9874 (speaker amplifier) - optional: if it did not load, the card comes up without the speaker
# codecs/snd-soc-mt6359-accdet (3.5 mm jack detection) is needed by the machine driver (symbol mt6359_accdet_enable_jack_detect): load it BEFORE that
for k in common/snd-soc-mtk-common codecs/snd-soc-mt6359 codecs/snd-soc-mt6359-accdet mt6785/snd-soc-mt6785-afe codecs/snd-soc-tfa9874 mt6785/mt6785-mt6359; do
	P=""
	if [ "$k" = codecs/snd-soc-tfa9874 ]; then
		insmod "$A/$k.ko" $P || echo "begonia-audio: tfa9874 did not load - no speaker"
		sync; continue
	fi
	# optional module parameters from /var/lib/begonia/params.<name> (e.g. mic_type0=3 for snd-soc-mt6359)
	[ -r "$S/params.$(basename $k)" ] && P=$(cat "$S/params.$(basename $k)")
	insmod "$A/$k.ko" $P || { echo "begonia-audio: insmod $k FAILED"; exit 1; }
	sync
done
# wait for the card
i=0; while [ $i -lt 20 ] && ! grep -q mt6785 /proc/asound/cards 2>/dev/null; do sleep 0.5; i=$((i+1)); done
C="amixer -c 0 -q"
# AFE route (no sound): DL1 -> ADDA, DAC in normal mode; output disabled, minimum volume
$C cset name='ADDA_DL_CH1 DL1_CH1' on
$C cset name='ADDA_DL_CH2 DL1_CH2' on
$C cset name='DAC In Mux' 'Normal Path'
# safety net (the AFE driver already sets NONE at start): TINYCONN I2S1/I2S3 pull DL1 by default -> the loudspeaker on I2S3
for m in I2S1_TINYCONN_CH1_MUX I2S1_TINYCONN_CH2_MUX I2S3_TINYCONN_CH1_MUX I2S3_TINYCONN_CH2_MUX; do $C cset name="$m" NONE; done
$C cset name='Headset Volume' 0,0
$C cset name='HP Mux' 'Open'
$C cset name='RCV Mux' 'Open'
# capture: main microphone on AIN0 (verified with claps 2026-11-12), both ADC channels from it, PGA gain 12 dB
$C cset name='UL1_CH1 ADDA_UL_CH1' on
$C cset name='UL1_CH2 ADDA_UL_CH2' on
$C cset name='ADDA_UL_Mux' 'MTKAIF'
$C cset name='UL_SRC_MUX' 'AMIC'
$C cset name='ADC_L_Mux' 'Left Preamplifier'
$C cset name='ADC_R_Mux' 'Right Preamplifier'
$C cset name='PGA_L_Mux' 'AIN0'
$C cset name='PGA_R_Mux' 'AIN0'
$C cset name='PGA1 Volume' 2
$C cset name='PGA2 Volume' 2
rm -f "$S/audio-starting"; sync
echo "begonia-audio: ready, card $(head -1 /proc/asound/cards)"
