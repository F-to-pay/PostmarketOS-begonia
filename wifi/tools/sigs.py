import re, os, subprocess
root = os.path.expanduser('~/gen4m-port')
names = ['mtk_cfg_add_key','mtk_cfg_get_key','mtk_cfg_del_key','mtk_cfg_set_default_mgmt_key','mtk_cfg_set_default_key',
 'mtk_cfg_tdls_mgmt','mtk_cfg_mgmt_frame_register','mtk_cfg_start_radar_detection','mtk_cfg_change_beacon','mtk_cfg_stop_ap',
 'mtk_cfg_set_bitrate_mask','mtk_cfg_get_txpower']
for n in names:
    out = subprocess.run(['grep','-rn','--include=*.c','--include=*.h',r'\b%s\b' % n, root], capture_output=True, text=True).stdout
    print('=== %s' % n)
    for l in out.strip().split('\n')[:6]:
        print('  ', l.replace(root + '/', '')[:150])
# signatures from the definitions
src = open(root + '/os/linux/gl_cfg80211.c', encoding='utf-8', errors='replace').read()
for n in names:
    m = re.search(r'\n(int|void|static int)\s+%s\s*\((.*?)\)\s*\n?\{' % n, src, re.S)
    print('\nDEF %s(%s)' % (n, re.sub(r'\s+', ' ', m.group(2)) if m else '?'))
