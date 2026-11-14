#!/bin/sh
# Build + load + hp test + summary of PMDBG/errors into /home/user/pm-trace.log
# Run: sh /home/user/Begonia/wifi/audio/trace-pm.sh   (no sudo before sh)
A=/home/user/Begonia/wifi/audio
OUT=/home/user/pm-trace.log
echo "== 1/3 build =="
sh "$A/build-on-phone.sh" || { echo "build failed"; exit 1; }
echo "== 2/3 loading =="
sh "$A/load-audio-safe.sh" || { echo "loading failed"; exit 1; }
echo "== 3/3 test =="
sudo dmesg -C 2>/dev/null || true
sh "$A/sound-test.sh" hp >/dev/null 2>&1
{
  echo "=== PMDBG (at load) ==="
  grep PMDBG /home/user/audio-load.log | cut -c1-200
  echo "=== uptime ==="; cat /proc/uptime
  echo "=== stream errors ==="
  dmesg | grep -E "hw_ptr err|memif enable|trigger|PMDBG" | head -10 | cut -c1-220
  echo "=== runtime_status ==="
  D=/sys/bus/platform/devices/11210000.clock-controller:audio-controller/power
  cat $D/runtime_status $D/control 2>&1
} 2>&1 | tee "$OUT"
echo "log: $OUT"
