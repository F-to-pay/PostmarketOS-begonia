<!-- Excerpt from the chronological log docs/NOTES-chronological.md (split by topic). -->
<!-- References like wifi/..., audio/..., pmos-begonia/... in the text refer to the old layout of the working directory; see README.md at the root of port/. -->

# IR transmitter — notes

### 📡 IR transmitter (IR TX) — WORKS AT THE HARDWARE LEVEL (2026-11-12 22:20), waiting for a visual check
- Hardware: the LED on **GPIO26 (function PWM_0)**, the PWM controller `pwm0` (0x11006000), channel 0 (vendor `mtk_irtx_pwm.c`, `pwm_ch=0`, memory mode). Mainline has a ready driver `pwm-ir-tx` (the kernel config already has `CONFIG_IR_PWM_TX=m`, `CONFIG_PWM_MEDIATEK=m`).
- DT: `audio/irtx-begonia.dtsi` (in the kernel tree `irtx-begonia.dtsi` + `#include` in begonia.dts; commit `982247293`): pinmux GPIO26→PWM_0, `&pwm0` status=okay, node `ir-transmitter` (`compatible = "pwm-ir-tx"`, `pwms = <&pwm0 0 26316>`, 38 kHz carrier). The DTB was installed by hand (backup `/root/boot-backup/*.pre-ir`), the modules autoload (`/etc/modules-load.d/begonia-ir.conf`: `pwm_mediatek`, `pwm-ir-tx`).
- Result: `/dev/lirc0` ("PWM IR Transmitter"), `ir-ctl --features`: send raw, scancode encoder, set carrier, set duty cycle. dmesg: "TX will not be accurate as PWM device might sleep" (a standard warning: the timings go through timers, not hardware). Installed `v4l-utils` (`ir-ctl`).
- Check: `ir-test` (blinks the IR for 6 s, look through the camera of another phone). A real remote target (air conditioner/TV) was not tested.
- IR pitfall: lirc rejects (EINVAL) a single write with a total duration of seconds (6 s); separate sends of 0.3 s and an NEC scancode pass. ir-test sends the bursts as separate calls.
- **IR TX confirmed by the user (22:30):** the IR LED glows in the camera of another phone (not noticed the first time — observer error). Status: WORKS. A real remote target was not tested.
- **Proximity confirmed (22:35):** stk3310 (iio:device3, in_proximity_raw): at rest 450–454, a hand nearby ~720 (threshold near=550 from patch 0003). Status: WORKS (checked with a hand; the near/far event for turning the screen off during a call was not checked — there are no calls).

