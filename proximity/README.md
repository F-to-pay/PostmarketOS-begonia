# STK3310 proximity/light sensor — Redmi Note 8 Pro

* DT: power and the proximity threshold — `patches/0003` (canonical: `../kernel-build/kernel/patches/`).
* `90-stk3310.rules` — udev rule; installed by `install-stk.sh`.
* `i2cdump.c`, `stkdump.sh` — reading the registers over I²C; `tools/` — tuning the gain/threshold (`stkboost.sh`, `stkregs.sh`, `stksweep.sh`, `prox.sh`).
* Result: at rest ≈ 450, a hand ≈ 720, threshold near = 550 (confirmed with a hand, 2026-11-12).
