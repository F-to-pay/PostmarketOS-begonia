/* SPDX-License-Identifier: GPL-2.0 */
/*
 * A minimal shim instead of the vendor wmt_exp.h (begonia / MT6785, mainline 6.16).
 * gen4m uses only ~17 functions of the WMT stack; the stack itself is replaced by the consys-bringup module here.
 * Implementation — shim/wmt_shim.c.
 */
#ifndef _WMT_EXP_H_
#define _WMT_EXP_H_

#include <linux/types.h>

typedef int INT32;
typedef unsigned int UINT32;
typedef unsigned char UINT8;
typedef unsigned char *PUINT8;
typedef void VOID;
typedef void *PVOID;
typedef signed char INT8;
typedef unsigned int *PUINT32;

typedef enum _MTK_WCN_BOOL {
	MTK_WCN_BOOL_FALSE = 0,
	MTK_WCN_BOOL_TRUE = 1
} MTK_WCN_BOOL;

typedef enum _ENUM_WMTDRV_TYPE_T {
	WMTDRV_TYPE_BT = 0,
	WMTDRV_TYPE_FM = 1,
	WMTDRV_TYPE_GPS = 2,
	WMTDRV_TYPE_WIFI = 3,
	WMTDRV_TYPE_WMT = 4,
	WMTDRV_TYPE_ANT = 5,
	WMTDRV_TYPE_STP = 6,
	WMTDRV_TYPE_SDIO1 = 7,
	WMTDRV_TYPE_SDIO2 = 8,
	WMTDRV_TYPE_LPBK = 9,
	WMTDRV_TYPE_COREDUMP = 10,
	WMTDRV_TYPE_GPSL5 = 11,
	WMTDRV_TYPE_MAX
} ENUM_WMTDRV_TYPE_T, *P_ENUM_WMTDRV_TYPE_T;

typedef enum _ENUM_WMTMSG_TYPE_T {
	WMTMSG_TYPE_POWER_ON = 0,
	WMTMSG_TYPE_POWER_OFF = 1,
	WMTMSG_TYPE_RESET = 2,
	WMTMSG_TYPE_STP_RDY = 3,
	WMTMSG_TYPE_HW_FUNC_ON = 4,
	WMTMSG_TYPE_MAX
} ENUM_WMTMSG_TYPE_T, *P_ENUM_WMTMSG_TYPE_T;

typedef enum _ENUM_WMTRSTMSG_TYPE_T {
	WMTRSTMSG_RESET_START = 0x0,
	WMTRSTMSG_RESET_END = 0x1,
	WMTRSTMSG_RESET_END_FAIL = 0x2,
	WMTRSTMSG_RESET_MAX,
	WMTRSTMSG_RESET_INVALID = 0xff
} ENUM_WMTRSTMSG_TYPE_T, *P_ENUM_WMTRSTMSG_TYPE_T;

typedef enum _ENUM_WMTCHIN_TYPE_T {
	WMTCHIN_CHIPID = 0x0,
	WMTCHIN_HWVER = WMTCHIN_CHIPID + 1,
	WMTCHIN_MAPPINGHWVER = WMTCHIN_HWVER + 1,
	WMTCHIN_FWVER = WMTCHIN_MAPPINGHWVER + 1,
	WMTCHIN_IPVER = WMTCHIN_FWVER + 1,
	WMTCHIN_ADIE = WMTCHIN_IPVER + 1,
	WMTCHIN_MAX,
} ENUM_WMT_CHIPINFO_TYPE_T, *P_ENUM_WMT_CHIPINFO_TYPE_T;

typedef void (*PF_WMT_CB)(ENUM_WMTDRV_TYPE_T src, ENUM_WMTDRV_TYPE_T dst, ENUM_WMTMSG_TYPE_T type,
			  void *buf, unsigned int size);

typedef int (*wmt_wlan_probe_cb)(void);
typedef int (*wmt_wlan_remove_cb)(void);
typedef int (*wmt_wlan_bus_cnt_get_cb)(void);
typedef int (*wmt_wlan_bus_cnt_clr_cb)(void);
typedef int (*wmt_wlan_emi_mpu_set_protection_cb)(bool);
typedef int (*wmt_wlan_is_wifi_drv_own_cb)(void);

typedef struct _MTK_WCN_WMT_WLAN_CB_INFO {
	wmt_wlan_probe_cb wlan_probe_cb;
	wmt_wlan_remove_cb wlan_remove_cb;
	wmt_wlan_bus_cnt_get_cb wlan_bus_cnt_get_cb;
	wmt_wlan_bus_cnt_clr_cb wlan_bus_cnt_clr_cb;
	wmt_wlan_emi_mpu_set_protection_cb wlan_emi_mpu_set_protection_cb;
	wmt_wlan_is_wifi_drv_own_cb wlan_is_wifi_drv_own_cb;
} MTK_WCN_WMT_WLAN_CB_INFO, *P_MTK_WCN_WMT_WLAN_CB_INFO;

/* what gen4m calls from the WMT stack; implemented in wmt_shim.c */
extern INT32 mtk_wcn_wmt_wlan_reg(P_MTK_WCN_WMT_WLAN_CB_INFO pWmtWlanCbInfo);
extern INT32 mtk_wcn_wmt_wlan_unreg(VOID);
extern INT32 mtk_wcn_wmt_msgcb_reg(ENUM_WMTDRV_TYPE_T eType, PF_WMT_CB pCb);
extern INT32 mtk_wcn_wmt_msgcb_unreg(ENUM_WMTDRV_TYPE_T eType);
extern UINT32 mtk_wcn_wmt_ic_info_get(ENUM_WMT_CHIPINFO_TYPE_T type);
extern MTK_WCN_BOOL mtk_wcn_wmt_assert_timeout(ENUM_WMTDRV_TYPE_T type, UINT32 reason, INT32 timeout);
extern MTK_WCN_BOOL mtk_wcn_wmt_assert_keyword(ENUM_WMTDRV_TYPE_T type, PUINT8 keyword);
extern MTK_WCN_BOOL mtk_wcn_wmt_func_on(ENUM_WMTDRV_TYPE_T type);
extern MTK_WCN_BOOL mtk_wcn_wmt_func_off(ENUM_WMTDRV_TYPE_T type);
extern int mtk_wcn_wmt_mpu_lock_aquire(void);
extern void mtk_wcn_wmt_mpu_lock_release(void);
extern int mtk_wcn_stp_coredump_start_get(void);
extern void mtk_wcn_set_connsys_power_off_flag(MTK_WCN_BOOL enable);

extern wmt_wlan_probe_cb mtk_wcn_wlan_probe;
extern wmt_wlan_remove_cb mtk_wcn_wlan_remove;
extern wmt_wlan_bus_cnt_get_cb mtk_wcn_wlan_bus_tx_cnt;
extern wmt_wlan_bus_cnt_clr_cb mtk_wcn_wlan_bus_tx_cnt_clr;
extern wmt_wlan_emi_mpu_set_protection_cb mtk_wcn_wlan_emi_mpu_set_protection;
extern wmt_wlan_is_wifi_drv_own_cb mtk_wcn_wlan_is_wifi_drv_own;

#endif /* _WMT_EXP_H_ */
