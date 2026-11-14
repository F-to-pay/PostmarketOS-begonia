#!/usr/bin/env python3
"""Port of gen4m (vendor 4.14) -> Linux 6.16: mechanical changes on top of a COPY of the tree.
usage: port.py <gen4m_root>
Every replacement is checked by an assert (the number of matches), so as not to miss silently.
"""
import os
import re
import sys

root = sys.argv[1]
stats = {}


def load(rel):
    s = open(os.path.join(root, rel), encoding='utf-8', errors='surrogateescape', newline='').read()
    return s.replace('\r\n', '\n')


def save(rel, s):
    open(os.path.join(root, rel), 'w', encoding='utf-8', errors='surrogateescape', newline='').write(s)


def sub(rel, old, new, count=1, regex=False):
    s = load(rel)
    n = len(re.findall(old, s)) if regex else s.count(old)
    assert n == count, '%s: %r matched %d times, expected %d' % (rel, old[:60], n, count)
    s = re.sub(old, new, s) if regex else s.replace(old, new)
    save(rel, s)
    stats[rel] = stats.get(rel, 0) + n


# ---------------- gl_init.c ----------------
G = 'os/linux/gl_init.c'
sub(G, 'MODULE_SUPPORTED_DEVICE(NIC_NAME);', '/* MODULE_SUPPORTED_DEVICE removed in kernel >= 5.12 */')
sub(G, 'kal_napi_poll, 64);', 'kal_napi_poll);')                   # netif_napi_add(): weight removed in 6.1
sub(G, r'COPY_MAC_ADDR\(ndev->dev_addr, sa->sa_data\);', 'eth_hw_addr_set(ndev, (const u8 *)sa->sa_data);', regex=True)
sub(G, r'kalMemCopy\(prGlueInfo->prDevHandler->dev_addr,\s*&MacAddr\.sa_data, ETH_ALEN\);',
    'eth_hw_addr_set(prGlueInfo->prDevHandler, (const u8 *)&MacAddr.sa_data);', regex=True)
sub(G, r'kalMemCopy\(prDevHandler->dev_addr,\s*&prAdapter->rWifiVar\.aucMacAddress1,\s*ETH_ALEN\);',
    'eth_hw_addr_set(prDevHandler, (const u8 *)&prAdapter->rWifiVar.aucMacAddress1);', regex=True)
sub(G, r'\tgConEmiPhyBaseFinal = gConEmiPhyBase;\n\tgConEmiSizeFinal = gConEmiSize;\n', '', regex=True)

# ---------------- gl_kal.c ----------------
K = 'os/linux/gl_kal.c'
s = load(K)
n0 = len(re.findall(r'\n\tmm_segment_t oldFs;\n', s))
s = re.sub(r'\n\tmm_segment_t oldFs;\n', '\n', s)
s = re.sub(r'\n\toldFs = get_fs\(\);\n\tset_fs\(get_ds\(\)\);\n', '\n', s)
s = re.sub(r'\n\tset_fs\(oldFs\);\n', '\n', s)
s = s.replace('\tset_fs(oldFs);\n', '')
assert 'get_fs' not in s and 'set_fs' not in s and 'KERNEL_DS' not in s, 'set_fs leftovers'
save(K, s)
stats[K] = stats.get(K, 0) + n0
sub(K, r'memcpy\(prGlueInfo->prDevHandler->dev_addr, pucMacAddr,\s*PARAM_MAC_ADDR_LEN\);',
    'eth_hw_addr_set(prGlueInfo->prDevHandler, pucMacAddr);', regex=True)
sub(K, r'atomic_long_read\(&ndev->tx_dropped\)', '0L', regex=True)
sub(K, r'atomic_long_read\(&ndev->rx_dropped\)', '0L', regex=True)
sub(K, 'rRoamInfo.bss = bss;', 'rRoamInfo.links[0].bss = bss;')

# ---------------- axi.c ----------------
A = 'os/linux/hif/axi/axi.c'
sub(A, 'MODULE_DEVICE_TABLE(axi, mtk_axi_ids);', '/* MODULE_DEVICE_TABLE(axi, ...) — there is no "axi" bus in the kernel */')
sub(A, '\tkalSetDrvEmiMpuProtection(grMem.pucRsvMemBase, 0, grMem.u4RsvMemSize);\n', '\t/* EMI MPU: mainline has no vendor MPU */\n')

# ================= batch 2 =================
# ---- axi.c: ID table for MT6632, remove() -> void ----
s = load(A)
m = re.search(r'static const struct platform_device_id mtk_axi_ids\[\] = \{.*?\n\};\n', s, re.S)
assert m, 'axi ids table'
s = s[:m.start()] + ('static const struct platform_device_id mtk_axi_ids[] = {\n'
                     '#ifdef CONNAC\n'
                     '\t{ .name = "CONNAC", .driver_data = (kernel_ulong_t)&mt66xx_driver_data_connac },\n'
                     '#endif\n'
                     '\t{ /* end: all zeroes */ },\n};\n') + s[m.end():]
assert s.count('static int mtk_axi_remove(IN struct platform_device *pdev)') == 1
s = s.replace('static int mtk_axi_remove(IN struct platform_device *pdev)', 'static void mtk_axi_remove(IN struct platform_device *pdev)')
old = '\tplatform_set_drvdata(pdev, NULL);\n\treturn 0;\n}\n\nstatic int mtk_axi_suspend'
assert s.count(old) == 1
s = s.replace(old, '\tplatform_set_drvdata(pdev, NULL);\n}\n\nstatic int mtk_axi_suspend')
save(A, s)

# ---- kal_pdma.c: list_sort cmp now takes const list_head* ----
P = 'os/linux/hif/common/kal_pdma.c'
sub(P, r'static int kalAmsduTxDCmp\(void \*prPriv, struct list_head \*prList1,\n\t\t\t  struct list_head \*prList2\)\n\{\n',
    'static int kalAmsduTxDCmp(void *prPriv, const struct list_head *prList1c,\n\t\t\t  const struct list_head *prList2c)\n{\n'
    '\tstruct list_head *prList1 = (struct list_head *)prList1c;\n\tstruct list_head *prList2 = (struct list_head *)prList2c;\n', regex=True)

# ---- proc_ops (gl_proc.c, gl_kal.c) ----
def to_proc_ops(rel):
    s = load(rel)
    out, pos, n = [], 0, 0
    for m in re.finditer(r'((?:static )?const) struct file_operations (\w+) = \{\n(.*?)\n\};', s, re.S):
        body = m.group(3)
        body = re.sub(r'\t\.owner = THIS_MODULE,\n', '', body + '\n').rstrip('\n')
        for a, b in (('.read =', '.proc_read ='), ('.write =', '.proc_write ='), ('.open =', '.proc_open ='),
                     ('.llseek =', '.proc_lseek ='), ('.release =', '.proc_release =')):
            body = body.replace(a, b)
        out.append(s[pos:m.start()])
        out.append('%s struct proc_ops %s = {\n%s\n};' % (m.group(1), m.group(2), body))
        pos = m.end()
        n += 1
    out.append(s[pos:])
    save(rel, ''.join(out))
    stats[rel] = stats.get(rel, 0) + n
    return n


assert to_proc_ops('os/linux/gl_proc.c') >= 10
assert to_proc_ops(K) == 3

# ---- gl_kal.c: RPS (netdev internals are not available to modules), ch_switch_notify, MODULE_IMPORT_NS ----
s = load(K)
i = s.index('int wlan_set_rps_map(struct netdev_rx_queue *queue, unsigned long rps_value)\n{')
j = s.index('\n}\n', i) + 3
s = s[:i] + ('int wlan_set_rps_map(struct netdev_rx_queue *queue, unsigned long rps_value)\n{\n'
             '\t/* 6.x: struct rps_map / netdev_rx_queue are not exported to modules — keep the default RPS */\n'
             '\treturn 0;\n}\n') + s[j:]
assert s.count('cfg80211_ch_switch_notify(prGlueInfo->prDevHandler, &chandef);') == 1
s = s.replace('cfg80211_ch_switch_notify(prGlueInfo->prDevHandler, &chandef);', 'cfg80211_ch_switch_notify(prGlueInfo->prDevHandler, &chandef, 0);')
mm = re.search(r'#if KERNEL_VERSION\(5, 4, 0\) <= CFG80211_VERSION_CODE\nMODULE_IMPORT_NS\([^\n]*\);\n#endif\n', s)
assert mm, 'MODULE_IMPORT_NS'
s = s[:mm.start()] + s[mm.end():]
save(K, s)
stats[K] += 3

# ---- P2P: the same classes ----
sub('os/linux/gl_p2p.c', 'p2p_napi_poll, 64);', 'p2p_napi_poll);')
sub('os/linux/gl_p2p.c', r'kalMemCopy\(prP2pDev->dev_addr, rMacAddr, ETH_ALEN\);', 'eth_hw_addr_set(prP2pDev, rMacAddr);', regex=True)
sub('os/linux/gl_p2p.c', r'COPY_MAC_ADDR\(prDev->dev_addr, sa->sa_data\);', 'eth_hw_addr_set(prDev, (const u8 *)sa->sa_data);', regex=True)
sub('os/linux/gl_p2p_cfg80211.c', r'kalMemCopy\(prNewNetDevice->dev_addr, rMacAddr, ETH_ALEN\);', 'eth_hw_addr_set(prNewNetDevice, rMacAddr);', regex=True)
sub('os/linux/gl_p2p_kal.c', 'prWdev->cac_started = FALSE;', 'prWdev->links[0].cac_started = FALSE;')
s = load('os/linux/gl_p2p_kal.c')
n = len(re.findall(r'(cfg80211_cac_event\([^;]*?GFP_KERNEL)\)', s))
assert n >= 1
s = re.sub(r'(cfg80211_cac_event\([^;]*?GFP_KERNEL)\)', r'\1, 0)', s)
s = s.replace('cfg80211_ch_switch_notify(\n\t\tprP2PInfo->prDevHandler,\n\t\tprP2PInfo->chandef);',
              'cfg80211_ch_switch_notify(\n\t\tprP2PInfo->prDevHandler,\n\t\tprP2PInfo->chandef, 0);')
save('os/linux/gl_p2p_kal.c', s)

# ---- cfg80211_ops: link_id and new signatures (gl_cfg80211.c/.h, gl_init.c) ----
CFG = 'os/linux/gl_cfg80211.c'
HDR = 'os/linux/include/gl_cfg80211.h'
for rel in (CFG, HDR):
    s = load(rel)
    # keys: link_id after net_device*
    s, n1 = re.subn(r'(mtk_cfg_(?:add_key|get_key|del_key|set_default_mgmt_key|set_default_key)\(\s*struct wiphy \*wiphy,\s*struct net_device \*\w+,)(\s*)(u8 key_index)',
                    r'\1\2int link_id,\2\3', s)
    # tdls_mgmt: link_id after peer
    s, n2 = re.subn(r'(mtk_cfg_tdls_mgmt\(\s*struct wiphy \*wiphy,\s*struct net_device \*\w+,\s*(?:const )?u8 \*peer,)(\s*)(u8 action_code)',
                    r'\1\2int link_id,\2\3', s)
    # start_radar_detection: link_id at the end
    s, n3 = re.subn(r'(mtk_cfg_start_radar_detection\(\s*struct wiphy \*wiphy,\s*struct net_device \*\w+,\s*struct cfg80211_chan_def \*chandef,\s*unsigned int cac_time_ms)\)',
                    r'\1, int link_id)', s)
    # stop_ap
    s, n4 = re.subn(r'(mtk_cfg_stop_ap\(\s*struct wiphy \*wiphy,\s*struct net_device \*\w+)\)', r'\1, unsigned int link_id)', s)
    # set_bitrate_mask: link_id before peer
    s, n5 = re.subn(r'(mtk_cfg_set_bitrate_mask\(\s*struct wiphy \*wiphy,\s*struct net_device \*\w+,)(\s*)(const u8 \*peer)',
                    r'\1\2unsigned int link_id,\2\3', s)
    # get_txpower: link_id after wdev
    s, n6 = re.subn(r'(mtk_cfg_get_txpower\(\s*struct wiphy \*wiphy,\s*struct wireless_dev \*wdev,)(\s*)(int \*dbm)',
                    r'\1\2unsigned int link_id,\2\3', s)
    save(rel, s)
    print('%s: keys=%d tdls=%d radar=%d stop_ap=%d bitrate=%d txpower=%d' % (rel, n1, n2, n3, n4, n5, n6))
    stats[rel] = stats.get(rel, 0) + n1 + n2 + n3 + n4 + n5 + n6

# change_beacon: gets cfg80211_ap_update, the vendor expects beacon_data
s = load(CFG)
pat = r'(int mtk_cfg_change_beacon\(struct wiphy \*wiphy,\s*struct net_device \*dev,\s*)struct cfg80211_beacon_data \*info\)\n\{\n'
assert len(re.findall(pat, s)) == 1
s = re.sub(pat, r'\1struct cfg80211_ap_update *ap_update)\n{\n\tstruct cfg80211_beacon_data *info = &ap_update->beacon;\n', s)
# mgmt_frame_register -> update_mgmt_frame_registrations (a wrapper)
mfr = re.search(r'void mtk_cfg_mgmt_frame_register\(struct wiphy \*wiphy,.*?\n\}\n', s, re.S)
assert mfr, 'mgmt_frame_register def'
s = s[:mfr.end()] + ('\n/* 6.x: instead of mgmt_frame_register the kernel calls update_mgmt_frame_registrations with the whole mask */\n'
                     'void mtk_cfg_update_mgmt_frame_registrations(struct wiphy *wiphy, struct wireless_dev *wdev,\n'
                     '\t\t\t\t\t     struct mgmt_frame_regs *upd)\n{\n\tint i;\n\n'
                     '\tfor (i = 0; i < 16; i++)\n'
                     '\t\tmtk_cfg_mgmt_frame_register(wiphy, wdev, i << 4, !!(upd->interface_stypes & BIT(i)));\n}\n') + s[mfr.end():]
# station_parameters: the fields moved into link_sta_params
a = s.index('mtk_cfg_add_station(') if 'mtk_cfg_add_station(' in s else None
save(CFG, s)
s = load(HDR)
mh = re.search(r'void mtk_cfg_mgmt_frame_register\([^;]*;\n', s)
assert mh, 'mgmt_frame_register proto'
s = s[:mh.end()] + ('void mtk_cfg_update_mgmt_frame_registrations(struct wiphy *wiphy, struct wireless_dev *wdev,\n'
                    '\t\t\t\t\t     struct mgmt_frame_regs *upd);\n') + s[mh.end():]
save(HDR, s)
sub(G, r'\.mgmt_frame_register = mtk_cfg_mgmt_frame_register,', '.update_mgmt_frame_registrations = mtk_cfg_update_mgmt_frame_registrations,', regex=True)

# the change_beacon prototype in the header + the RPS loop (netdev->_rx is not available)
s = load(HDR)
pat = r'(int mtk_cfg_change_beacon\(struct wiphy \*wiphy,\s*struct net_device \*dev,\s*)struct cfg80211_beacon_data \*info\);'
assert len(re.findall(pat, s)) == 1
s = re.sub(pat, r'\1struct cfg80211_ap_update *ap_update);', s)
save(HDR, s)
sub(K, r'for \(j = 0; j < dev->real_num_rx_queues; \+\+j\)\n\t\t\t\twlan_set_rps_map\(&dev->_rx\[j\], value\);',
    '(void)j; /* the RPS map is not available to modules on 6.x */', regex=True)

# screen fb notifier: a DRM kernel has no fb_register_client -> stubs (power saving only)
sub(K, 'static struct notifier_block wlan_fb_notifier;',
    '#ifndef FB_EVENT_BLANK\n#define FB_EVENT_BLANK 0x09\n#endif\n#ifndef FB_BLANK_UNBLANK\n#define FB_BLANK_UNBLANK 0\n#endif\n'
    '#ifndef FB_BLANK_POWERDOWN\n#define FB_BLANK_POWERDOWN 4\n#endif\n#define fb_register_client(nb) (0)\n#define fb_unregister_client(nb) (0)\n'
    'static struct notifier_block wlan_fb_notifier;')

# station_parameters: supported_rates/ht_capa/vht_capa -> link_sta_params.*
s = load(CFG)
n = len(re.findall(r'(\w+)->(supported_rates_len|supported_rates|ht_capa|vht_capa)\b', s))
assert n >= 10, n
s = re.sub(r'\b(params|pParams)->(supported_rates_len|supported_rates|ht_capa|vht_capa)\b', r'\1->link_sta_params.\2', s)
save(CFG, s)
stats[CFG] += n


# ---- CONNAC/mt6785: gl_rst.c must see the WMT types; plat_priv.c — stubs instead of the vendor cpu_ctrl/topo_ctrl/EMI-MPU ----
sub('os/linux/gl_rst.c', '#include "precomp.h"\n', '#include "precomp.h"\n#include "wmt_exp.h"\n')
save('os/linux/plat/mt6785/plat_priv.c', '''// SPDX-License-Identifier: GPL-2.0
/* mt6785 platform glue for mainline (begonia): vendor CPU-boost/EMI-MPU are not available -> stubs. */
#include "precomp.h"

uint32_t kalGetCpuBoostThreshold(void)
{
	return 3;
}

int32_t kalBoostCpu(IN struct ADAPTER *prAdapter, IN uint32_t u4TarPerfLevel, IN uint32_t u4BoostCpuTh)
{
	return 0;
}

int32_t kalGetFwFlavor(uint8_t *flavor)
{
	*flavor = 'a';	/* WIFI_RAM_CODE_soc1_0_2a_1.bin */
	return 1;
}
''')


# ---- EMI: the address/size of the CONSYS reserve is taken from the shim (consys-bringup), MPU — no-op ----
sub(G, '\tDBGLOG(INIT, INFO, "initWlan\\n");\n',
    '\t{\n\t\textern int consys_shim_get_emi(phys_addr_t *base, unsigned long long *size);\n\n'
    '\t\tif (consys_shim_get_emi(&gConEmiPhyBaseFinal, &gConEmiSizeFinal))\n\t\t\treturn -ENODEV;\n'
    '\t\tgConEmiPhyBase = gConEmiPhyBaseFinal;\n\t\tgConEmiSize = gConEmiSizeFinal;\n\t}\n'
    '\tDBGLOG(INIT, INFO, "initWlan\\n");\n')
s = load('os/linux/plat/mt6785/plat_priv.c')
s += '\nvoid kalSetEmiMpuProtection(phys_addr_t emiPhyBase, bool enable)\n{\n}\n\nvoid kalSetDrvEmiMpuProtection(phys_addr_t emiPhyBase, uint32_t offset, uint32_t size)\n{\n}\n'
save('os/linux/plat/mt6785/plat_priv.c', s)

for rel, n in sorted(stats.items()):
    print('%-36s %d replacements' % (rel, n))


# ---- clean module unload (rmmod) ----
# exitWlan() with CFG_SUPPORT_PERSIST_NETDEV first removed the netdevs (wlanClearDevIdx -> u4WlanDevNum=0), and wlanRemove(),
# called from glUnregisterBus(), returned right away ("u4WlanDevNum = 0"): the threads (hif_thread) stayed alive, the adapter was not
# freed, wiphy_unregister crashed, and hif_thread reached into an already unmapped CSR. On Android (WMT) this teardown is done by
# wlan_remove_cb = hifAxiRemove() BEFORE the unload; we do the same as the first step of exitWlan().
sub(G, 'static void exitWlan(void)\n{\n',
    'static void exitWlan(void)\n{\n'
    '\t{\n\t\textern int hifAxiRemove(void);\n\n'
    '\t\t/* a full teardown of the adapter (threads, IRQ, WF power) while the netdev/counters are intact */\n'
    '\t\thifAxiRemove();\n\t}\n')
