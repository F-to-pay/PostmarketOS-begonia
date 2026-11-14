import os, re, subprocess
root = os.path.expanduser('~/gen4m-port')
names = ['connectivity_export_show_stack','get_low_latency_mode','mtk_wcn_consys_hw_wifi_paldo_ctrl','mtk_wcn_set_connsys_power_off_flag',
 'mtk_wcn_wmt_assert_timeout','mtk_wcn_wmt_mpu_lock_aquire','mtk_wcn_wmt_mpu_lock_release','register_file_buf_handler',
 'register_set_p2p_mode_handler','update_driver_loaded_status','wifi_reset_end','wifi_reset_start','mtk_wcn_wmt_assert_keyword',
 'mtk_wcn_wmt_ic_info_get','mtk_wcn_wmt_wifi_fem_cfg_report','wireless_send_event']
for n in names:
    out = subprocess.run(['grep','-rnE','--include=*.c','--include=*.h',r'\b%s\b' % n, root], capture_output=True, text=True).stdout.strip().split('\n')
    print('== %s' % n)
    for l in out[:4]:
        print('   ', l.replace(root + '/', '')[:170])
