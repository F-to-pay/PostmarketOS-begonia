#!/bin/sh
# Initial mixer state of MT6785 + MT6359 (the drivers in the kernel load by themselves from DT).
# Safe default: the headphone output is disabled (HP Mux=Open), minimum volume. Enable: hp-on [0-4]; disable: hp-off.
i=0
while [ $i -lt 40 ] && ! grep -q mt6785 /proc/asound/cards 2>/dev/null; do sleep 0.5; i=$((i+1)); done
grep -q mt6785 /proc/asound/cards 2>/dev/null || { echo "begonia-audio: no mt6785 card"; exit 1; }
C="amixer -c mt6785mt6359 -q"
# AFE route: DL1 -> ADDA, DAC in normal mode
$C cset name='ADDA_DL_CH1 DL1_CH1' on
$C cset name='ADDA_DL_CH2 DL1_CH2' on
$C cset name='DAC In Mux' 'Normal Path'
# TINYCONN I2S1/I2S3 pull DL1 by default -> the loudspeaker on I2S3; the speaker is enabled only through spk-on
for m in I2S1_TINYCONN_CH1_MUX I2S1_TINYCONN_CH2_MUX I2S3_TINYCONN_CH1_MUX I2S3_TINYCONN_CH2_MUX; do $C cset name="$m" NONE; done
$C cset name='Headset Volume' 0,0
$C cset name='HP Mux' 'Open'
$C cset name='RCV Mux' 'Open'
# capture: main microphone AIN0 on both ADC channels, PGA 12 dB
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
echo "begonia-audio: ready, $(head -1 /proc/asound/cards)"
