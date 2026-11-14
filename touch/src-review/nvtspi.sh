cd ~/miker/drivers/input/touchscreen/NT36672A_G7
echo "== NVTSPI-related functions"; grep -n -E "NVTSPI|nvt_flash_(read|write|open)|DEVICE_NAME|proc_create\(DEVICE_NAME|static (ssize_t|int32_t) nvt_flash" nt36xxx.c | head -20
n=$(grep -n "NVT_proc_entry = proc_create" nt36xxx.c | head -1 | cut -d: -f1); echo "proc_create line: $n"
s=$(grep -n "^static ssize_t nvt_flash_write\|nvt_flash_write(struct file" nt36xxx.c | head -1 | cut -d: -f1); echo "write handler line: $s"
if [ -n "$s" ]; then a=$((s-5)); b=$((s+110)); sed -n "${a},${b}p" nt36xxx.c; fi
