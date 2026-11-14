set -e
rm -rf ~/nvtpanel && mkdir -p ~/nvtpanel && cd ~/nvtpanel
cp ~/ntsrc/drivers/gpu/drm/panel/panel-novatek-nt36672a.c .
python3 - <<'PY'
p = 'panel-novatek-nt36672a.c'
s = open(p).read()
old = '''static const struct drm_display_mode csot_fhd_video_panel_default_mode = {
	.clock		= (1080 + 64 + 16 + 56) * (2340 + 14 + 2 + 10) * 60 / 1000,

	.hdisplay	= 1080,
	.hsync_start	= 1080 + 64,
	.hsync_end	= 1080 + 64 + 16,
	.htotal		= 1080 + 64 + 16 + 56,

	.vdisplay	= 2340,
	.vsync_start	= 2340 + 14,
	.vsync_end	= 2340 + 14 + 2,
	.vtotal		= 2340 + 14 + 2 + 10,'''
new = '''static const struct drm_display_mode csot_fhd_video_panel_default_mode = {
	/* Xiaomi bootloader values for begonia CSOT: vfp 12 (not 14), fps 60.24 */
	.clock		= (1080 + 64 + 16 + 56) * (2340 + 12 + 2 + 10) * 6024 / 100 / 1000,

	.hdisplay	= 1080,
	.hsync_start	= 1080 + 64,
	.hsync_end	= 1080 + 64 + 16,
	.htotal		= 1080 + 64 + 16 + 56,

	.vdisplay	= 2340,
	.vsync_start	= 2340 + 12,
	.vsync_end	= 2340 + 12 + 2,
	.vtotal		= 2340 + 12 + 2 + 10,'''
assert s.count(old) == 1, s.count(old)
open(p, 'w').write(s.replace(old, new))
print('patched; clock =', (1080+64+16+56)*(2340+12+2+10)*6024//100//1000, 'kHz')
PY
echo "obj-m += panel-novatek-nt36672a.o" > Makefile
make -C ~/ntsrc M=$HOME/nvtpanel ARCH=arm64 LLVM=1 KBUILD_MODPOST_WARN=1 modules 2>&1 | grep -E "LD \[M\]|error|Error" | head
ls -la *.ko; modinfo panel-novatek-nt36672a.ko | grep -E "^(name|vermagic|depends)"
cp panel-novatek-nt36672a.ko /mnt/c/Users/1342c/workspace/pmos-begonia/nvtpanel/
