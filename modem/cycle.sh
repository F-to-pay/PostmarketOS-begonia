#!/bin/bash
# One full modem test cycle from the PC: phone reboot -> chroot with COPIES of nvdata/protect -> module -> ccci_fsd -> summary.
# usage: md/cycle.sh <run-name> [wait-seconds=60] [extra module parameters]
# Needs: ssh key ~/.ssh/id_ed25519_claude, phone 10.233.161.187, module/md-bringup.ko, md/md-chroot.sh, md-chroot.tgz
set -u
NAME=${1:?run name}; WAIT=${2:-60}; EXTRA=${3:-}
W=$(cd "$(dirname "$0")/.." && pwd)
H=10.233.161.187
SSH="ssh -o ConnectTimeout=8 -i $HOME/.ssh/id_ed25519_claude user@$H"
mkdir -p "$W/md-logs"
echo "[1/6] reboot"
$SSH 'sudo systemctl stop md-fsd md-rpcd 2>/dev/null; sudo rmmod md_bringup 2>/dev/null; sudo sh ~/md-chroot.sh teardown >/dev/null 2>&1; rm -f ~/nvdata-copy.img ~/protect1-copy.img ~/protect2-copy.img; sudo systemctl reboot' >/dev/null 2>&1
sleep 55
for i in $(seq 1 40); do $SSH uptime >/dev/null 2>&1 && break; sleep 4; done
echo "[2/6] copying the module/script"
scp -q -i $HOME/.ssh/id_ed25519_claude "$W/module/md-bringup.ko" "$W/md/md-chroot.sh" user@$H:/home/user/
echo "[3/6] chroot (copies of nvdata/protect1/protect2)"
$SSH 'sudo NVDATA_COPY=/home/user/nvdata-copy.img sh ~/md-chroot.sh setup 2>&1 | head -1'
echo "[4/6] start (kmsg -> md-logs/$NAME.kmsg)"
(ssh -o ServerAliveInterval=2 -i $HOME/.ssh/id_ed25519_claude user@$H 'sudo cat /dev/kmsg' > "$W/md-logs/$NAME.kmsg" 2>&1 &)
sleep 3
$SSH "sh -c 'sudo insmod ~/md-bringup.ko stage=6 mmpu=1 ungate=1 watch_ms=0 $EXTRA; echo insmod=\$?; sudo systemd-run --unit=md-fsd --collect sh /home/user/md-chroot.sh run >/dev/null 2>&1; sudo systemd-run --unit=md-rpcd --collect sh /home/user/md-chroot.sh run-rpc >/dev/null 2>&1; sleep $WAIT; systemctl is-active md-fsd'" 2>&1 | head -4
echo "[5/6] summary"
python -X utf8 "$W/md/summary.py" "$W/md-logs/$NAME.kmsg"
echo "[6/6] exception record (if any) and property keys"
$SSH "sh ~/q2.sh >/dev/null 2>&1; strings -n 6 /tmp/nc.bin | grep -E '^mcu/' | sort -u | head -3; xxd -s 0x970 -l 0x10 /tmp/nc.bin; echo props:; sort /tmp/props.log 2>/dev/null | uniq -c; tail -3 /tmp/ccci_rpcd.log" 2>&1 | head -6
