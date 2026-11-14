# Redmi Note 8 Pro (begonia): internal audio (headphones + built-in microphone) works on mainline 6.16.4, with four traps worth knowing

DRAFT, not sent. Tested on mainline 6.16.4 (`linux-postmarketos-mediatek-mt6785-6.16.4-r1`), Redmi Note 8 Pro (MT6785), postmarketOS edge, PulseAudio. Vendor reference: MiCode `Xiaomi_Kernel_OpenSource`, branch `begonia-r-oss` (`sound/soc/mediatek/mt6785`, `codec/mt6359`).

## Result

Out-of-tree (4 modules, built on the phone), but reproducible. After boot the card `mt6785_mt6359` appears and:

- **playback to the 3.5 mm headphones** works through PulseAudio (sink `analog-stereo-out`, 48 kHz, tested by ear, music and test tones), the volume slider is linear and monotonic;
- **the built-in (main) microphone** works through PulseAudio (source `analog-stereo-in`, 48 kHz stereo): speech and claps are captured at a usable level, with noise (noise floor about -60 dBFS, which for this phone is as good as it gets);
- modules are loaded at boot by a small systemd unit, and the card/profile is picked up by PulseAudio without manual steps.

**Not done / not tested:** the **loudspeaker** (external SmartPA TFA9874 on `i2c6@0x34`, I2S3), the headset microphone, the second (noise-cancelling) microphone, the earpiece, line-out, suspend/resume, DMIC. Bluetooth A2DP is independent of this port and was done earlier (see the WiFi/BT report).

This is *not* upstreamable as is: it is a fork of mainline `sound/soc/mediatek/mt8192` (+ `common/`, + the mainline `mt6359.c`) with a cut-down machine driver, partly with Russian comments, and the clock/gate side is worked around rather than fixed. I'm writing it up because the **traps** are the expensive part and apply to anyone doing audio on MT67xx/MT6359, and so that nobody spends days on the same dead ends.

## Approach

The vendor driver is 194 KB (ION/SCP/audio_dsp). Not needed: the AFE register layout of MT6785 is the same generation as mainline **mt8192** (2441 of 2450 register defines are identical; only `AFE_ASRC_2CH_CON12/13` and a few gain masks differ). So: mt8192 AFE + `common/` + mainline `mt6359.c`, machine driver reduced to "MT6359 only" (FE: DL1/DL12/DL2/DL3, UL1/UL2/UL3/UL_mono1; BE: ADDA, ADDA_CH34). Changes against mt8192 that were needed to even probe: `top_apll{1,2}_d4` -> `_d8` (MT6785 top-clk has d2/d4/d8 as parents of ENG1/2, the vendor uses d8), `audsys` reset optional, `AFE_MAX_REGISTER = AFE_DL8_END`, the two ASRC registers from the vendor tree. DT: `&audsys` + power-domain `AUDIO` + `afe: audio-controller` (`mediatek,mt6785-audio`, SPI 184 level-low, 33 clocks in the order of `aud_clks[]`, plus the `aud_dac/dac_predis/…` clocks that the DAPM clock-supply widgets need, otherwise `Failed to add route`), MTKAIF pin states (GPIO143/146/189-194) and the `sound` node (`mediatek,mt6785_mt6359`, legacy probe).

## What it took (the traps, in the order I hit them)

### 1. `insmod` hangs the bus without any log: ASRC clock gates

`AUDIO_TOP_CON1` bits 12-14 (`aud_conn_i2s`, `aud_general1`, `aud_general2`) gate the ASRC block (`0x0c80..`). Reading any register there while they are gated hangs the AP hard: no oops, no message, only a reboot. The regmap cache init (`num_reg_defaults_raw`) reads the whole range, so a plain `insmod` of the AFE module hung. I found it by bisecting with probe stages and an `O_SYNC` log file. Workaround: clear `0x7000` in `AUDIO_TOP_CON1` in `enable_clock` and `runtime_resume` (a proper fix is to take the three clocks through CCF).

### 2. Runtime PM was dead because of my own double disable

`devm_pm_runtime_enable()` already disables on unbind; an extra `pm_runtime_disable()` in `remove()` made `disable_depth` grow on every `rmmod` (4-5 after a few tests). PM stayed off, `cache_only` never cleared, `memif enable` failed with `-EBUSY`. After removing the manual call `disable_depth` is 1 and resume/suspend work. Related: once a repeated `insmod` of the AFE module after `rmmod` hung the whole system - I reboot between tests.

### 3. The real blocker: PCM runs, but the AFE engine does not tick (`AUDIO_TOP_CON0` bit 2)

Symptoms for a long time: the card opens, DAPM powers the whole chain (I traced `snd_soc_dapm_widget_power`: ADDA, MTKAIF, MT6359 DAC/HP, `aud_dac` clocks, `AFE_ON`), the stream is RUNNING, but `AFE_DL1_CUR` never moves, `AFE_IRQ_MCU_STATUS` is 0, IRQ 184 never fires and `aplay` ends with `Write error -5`. Everything I could check was right: `AFE_DAC_CON0` DL1+AFE_ON, `AFE_CONN3/4` routing, `AFE_ADDA_DL_SRC2_CON0` bit 0, IRQ enable/count/fs, `AFE_IRQ_MCU_EN`, the DL1 memif config identical to the vendor's.

Cause: **`AUDIO_TOP_CON0` bit 2 (`aud_afe`, the AFE core clock gate) stays set** while CCF reports `aud_afe` as enabled (`clk_summary`: enable_count 1, `clk_enable: aud_afe` in the trace). The gates in `clk-mt6785-aud.c` are *not* inverted (`mtk_clk_gate_ops_no_setclr`: bit set = clock off), so CCF's enable should clear it, and it does not stick. With the bit set the engine does not run at all. I found it by clearing individual `AUDIO_TOP_CON0` bits while a stream was running (a module parameter that did `regmap_update_bits(0x0, bits, 0)` from the dump worker): the moment bit 2 was cleared `AFE_DL1_CUR` started to run. Fix: after `regcache_sync()` in `runtime_resume` force-write the bit to 0 (`regmap_update_bits_base(..., force=true)`). I did **not** find out why CCF's own clear does not take effect (my suspicion is something re-setting it, or two regmaps over the same syscon); an ablation showed that without the explicit clear the stream never starts, with it it starts on the first stream after a cold boot.

Misleading side effect while debugging: the AFE/`AUDIO_TOP_CON0` state survives a *warm reboot* (while PMIC `HP Mux` does not), so a manual "ungate" experiment made the next boot work "by itself" and gave a false success.

### 4. The mt6359 playback volume scale is inverted

In the copy of `mt6359.c` I used, `Headset Volume`, `Lineout Volume` and `Handset Volume` are `SOC_*_EXT_TLV(..., 0x12, 0, ...)` with TLV `-10 dB .. +8 dB` (increasing), but the hardware gain code **decreases** with the value (`DL_GAIN_8DB = 0 ... DL_GAIN_N_10DB = 18`, `DL_GAIN_N_22DB = 30`, `0x1f` = -40 dB) and the custom `get` returns the raw code. So control value 0 was **+8 dB (the maximum)**, and the ALSA scale lied in the dangerous direction: "minimum volume" tests were at maximum analog gain (and hurt a bit, see below). A PulseAudio slider that drives this control (the sink has hardware volume) got *quieter* above ~50 % for the same reason.

Fix: `invert = 1` on the three controls and `mt6359_gain_to_idx()` (index = max - code, codes outside the scale reported as 0) in the custom `get`. Verified on the PMIC register `0x260c` (`ZCD_CON2`) through debugfs: index 0 -> code 18 (`0x0912`), top index -> `0x0000`. The headset range was also extended downwards (index 0..20 = -12..+8 dB, codes 19/20 are valid) because the output is very hot on this board. **Worth a look at the upstream driver for anyone using MT6359 audio.**

### 5. Microphone: needs mic type DCC; the main mic is AIN0

MTKAIF calibration passes (`chosen_phase 2/2/-1`, `phase_cycle 3/3/0`, printed after I raised the `dev_dbg` to `dev_info`), the capture chain powers up (`MTKAIF_TX`, ADC, PGA, `AIN0`, `MIC_BIAS_0`, bias 1.9 V verified in PMIC regs), `AFE_VUL12_CUR` runs. But the data was all zeros (one channel) or at the noise level of a dead input (the other). The inputs were found with a "5 segments, 3 s each, user claps" script that switches `PGA_L/R_Mux` and `ADC_L/R_Mux` and compares rms/peak per segment: only **AIN0** reacts (peaks 6877 / 10085 against a floor of ~30), AIN1-3 do not.

The weak signal was the **mic type**: the driver default is `MIC_TYPE_MUX_IDLE`; at PGA 12 dB the output was almost zero in silence and at PGA 24 dB it vanished completely (noise *decreased* with more gain, which a real amplifier never does) - the preamp saturates on the input DC offset. With **DCC** (`mediatek,mic-type-0` / the driver default changed to `MIC_TYPE_MUX_DCC`) the noise floor is normal and the signal comes through. The vendor DT of this board has no mic-type property (set by the Android HAL), so there is nothing to copy; `mediatek,mic-type-0` is not in the begonia DT yet.

### 6. pmOS packaging traps

- **`alsa-restore.service` / `alsa-state.service`** restore the mixer saved at the last shutdown at every boot. With the first working stream that meant: headphone path enabled, analog gain at maximum (see 4), digital gain reset. I masked both and set the defaults from the module-load script (headphone output *disabled* by default, `HP Mux = Open`; a `hp-on` helper enables it with a capped gain).
- **No UCM / PulseAudio profile for the card.** The default "fallback" profile is output-only and points at `hw:X,0`; the capture PCM is `hw:X,4`. I added a PulseAudio profile-set (output `hw:%f,0`, input `hw:%f,4`) selected through a udev rule (`ENV{PULSE_PROFILE_SET}`).
- **PulseAudio uses the codec's dB scale for the slider** and combines hardware steps with software volume, which gives an uneven curve. `module-udev-detect ignore_dB=1` makes the slider map linearly onto the 21 analog steps (1 dB each).
- `apk upgrade` re-runs `mkinitfs`/`boot-deploy` and reverts any custom DTB (same trap as in the WiFi report): the audio nodes live only in the DTB I install.

## Reproduce

Modules built against the phone kernel (vermagic `6.16.4 SMP preempt mod_unload aarch64`, same config), loaded in this order:

```
insmod snd-soc-mtk-common.ko
insmod snd-soc-mt6359.ko                 # mic type defaults to DCC
insmod snd-soc-mt6785-afe.ko             # dl_gain=2165 (default; see below)
insmod mt6785-mt6359.ko                  # machine driver: card appears, MTKAIF calibration
# routes (no UCM): ADDA_DL_CH1/2 <- DL1_CH1/2, DAC In Mux = Normal Path, HP Mux = Audio Playback (to hear it);
# capture: UL1_CH1/2 <- ADDA_UL_CH1/2, ADDA_UL_Mux = MTKAIF, UL_SRC_MUX = AMIC, ADC_L/R_Mux = Left/Right Preamplifier,
#          PGA_L_Mux = AIN0, PGA_R_Mux = AIN0, PGA1/2 Volume = 2
```

DT additions: `&audsys` power-domain, the `afe` node and the `sound` node (appended to `mt6785-xiaomi-begonia.dts`); the base DT must provide the `mt6359` codec node. `dl_gain` is a module parameter of the AFE module (linear, /65536) for the digital gain of the ADDA DL SRC: on this board the output is so hot that the owner's comfortable maximum was about -15..-20 dBFS even at the codec's lowest analog gain. Treat the gains (PGA 12 dB, PulseAudio source 215 %, `dl_gain`) as one person's preferences, not facts.

A silent check of the playback path: `aplay -D hw:0,0 -f S16_LE -r 48000 -c 2 -d 2 /dev/zero` with the routes above must return 0 and `asys-isr` in `/proc/interrupts` must increase; with the bit 2 problem it returns `-EIO` and the counter stays 0.

## Remaining (known) issues

- Why CCF does not really ungate `aud_afe` (bit 2), and proper CCF clocks for the ASRC gates (`aud_conn_i2s`, `aud_general1/2`) instead of the register workarounds.
- **Loudspeaker:** TFA9874 (`i2c6@0x34`, speech on I2S3 GPIO67/68/71, reset GPIO24, irq GPIO69). Mainline `tfa989x` knows 9890/91/95/97 only; this needs either a port of the vendor `tfa98xx` or an extension of `tfa989x`.
- Headset mic and the noise-cancelling mic (other AIN inputs / mic types), earpiece, `aud_tdm_clk` missing, occasional `ASoC error (-16)` on registers `0x052c`/`0xe40`.
- PulseAudio advertises 44.1 kHz for the sink while the machine driver constrains playback to 48 kHz (the server resamples).
- A ~100 ms click at the start of every capture (MTKAIF start-up).
- Some module parameters from the hang bisection (`stage`, `ungate0/1`, `keep_intbus`) are still in the AFE driver.
- When the AFE is stopped the level IRQ line chatters (`status 0`); the handler's error print is rate-limited because it flooded dmesg (thousands per second).

## Questions

1. Is there interest in the AFE part as an upstream driver (the mt8192-derived `mt6785` AFE + machine driver, DT bindings for `mediatek,mt6785-audio`), or should it stay a downstream pmOS package?
2. Does anyone know why a CCF enable of `aud_afe` (`mtk_clk_gate_ops_no_setclr` on `AUDIO_TOP_CON0` bit 2) does not clear the bit on MT6785, and whether the three ASRC gates should be modelled in `clk-mt6785-aud.c`?
3. mt6359: is the inverted gain scale known/already fixed upstream? And should `mediatek,mic-type-0` be set for this board in the DT (DCC)?
4. For pmOS: is it acceptable to ship a PulseAudio profile-set + udev rule for the card, mask `alsa-restore/alsa-state` and set the mixer defaults from a boot script, or is a UCM profile the preferred way?

One more thing for the record, because it affects how tests should be done: while the scale in (4) was still inverted, an innocent-looking "minimum volume" tone test played into headphones I was wearing was loud enough to hurt for a few seconds. All later playback tests used digital silence or a -40 dBFS tone with the output disabled by default. If you test this, do not wear the headphones for the first runs.

Everything above is from `NOTES.md` in my working tree (the audio sections at the end); I can share the modules and the patched trees.
