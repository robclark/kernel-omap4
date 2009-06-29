/*
 *  Copyright (C) 2003-2005 Pontus Fuchs, Giridhar Pemmasani
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 */

#ifndef _IW_NDIS_H_
#define _IW_NDIS_H_

#include "ndis.h"

#define	WL_NOISE	-96	/* typical noise level in dBm */
#define	WL_SIGMAX	-32	/* typical maximum signal level in dBm */

struct ndis_encr_key {
	ULONG struct_size;
	ULONG index;
	ULONG length;
	UCHAR key[NDIS_ENCODING_TOKEN_MAX];
};

struct ndis_add_key {
	ULONG struct_size;
	ndis_key_index index;
	ULONG length;
	mac_address bssid;
	UCHAR pad[6];
	ndis_key_rsc rsc;
	UCHAR key[NDIS_ENCODING_TOKEN_MAX];
};

struct ndis_remove_key {
	ULONG struct_size;
	ndis_key_index index;
	mac_address bssid;
};

struct ndis_fixed_ies {
	UCHAR time_stamp[8];
	USHORT beacon_interval;
	USHORT capa;
};

struct ndis_variable_ies {
	ULONG elem_id;
	UCHAR length;
	UCHAR data[1];
};

enum ndis_reload_defaults { Ndis802_11ReloadWEPKeys };

struct ndis_assoc_info {
	ULONG length;
	USHORT req_ies;
	struct req_ie {
		USHORT capa;
		USHORT listen_interval;
		mac_address cur_ap_address;
	} req_ie;
	ULONG req_ie_length;
	ULONG offset_req_ies;
	USHORT resp_ies;
	struct resp_ie {
		USHORT capa;
		USHORT status_code;
		USHORT assoc_id;
	} resp_ie;
	ULONG resp_ie_length;
	ULONG offset_resp_ies;
};

struct ndis_configuration_fh {
	ULONG length;
	ULONG hop_pattern;
	ULONG hop_set;
	ULONG dwell_time;
};

struct ndis_configuration {
	ULONG length;
	ULONG beacon_period;
	ULONG atim_window;
	ULONG ds_config;
	struct ndis_configuration_fh fh_config;
};

struct ndis_wlan_bssid {
	ULONG length;
	mac_address mac;
	UCHAR reserved[2];
	struct ndis_essid ssid;
	ULONG privacy;
	ndis_rssi rssi;
	UINT net_type;
	struct ndis_configuration config;
	UINT mode;
	ndis_rates rates;
};

struct ndis_wlan_bssid_ex {
	ULONG length;
	mac_address mac;
	UCHAR reserved[2];
	struct ndis_essid ssid;
	ULONG privacy;
	ndis_rssi rssi;
	UINT net_type;
	struct ndis_configuration config;
	UINT mode;
	ndis_rates_ex rates_ex;
	ULONG ie_length;
	UCHAR ies[1];
};

/* we use bssid_list as bssid_list_ex also */
struct ndis_bssid_list {
	ULONG num_items;
	struct ndis_wlan_bssid bssid[1];
};

enum ndis_priv_filter {
	Ndis802_11PrivFilterAcceptAll, Ndis802_11PrivFilter8021xWEP
};

enum network_type {
	Ndis802_11FH, Ndis802_11DS, Ndis802_11OFDM5, Ndis802_11OFDM24,
	/* MSDN site uses Ndis802_11Automode, which is not mentioned
	 * in DDK, so add one and assign it to
	 * Ndis802_11NetworkTypeMax */
	Ndis802_11Automode, Ndis802_11NetworkTypeMax = Ndis802_11Automode
};

struct network_type_list {
	ULONG num;
	enum network_type types[1];
};

enum ndis_power {
	NDIS_POWER_OFF = 0, NDIS_POWER_MAX, NDIS_POWER_MIN,
};

struct ndis_auth_req {
	ULONG length;
	mac_address bssid;
	ULONG flags;
};

struct ndis_bssid_info {
	mac_address bssid;
	ndis_pmkid_vavlue pmkid;
};

struct ndis_pmkid {
	ULONG length;
	ULONG bssid_info_count;
	struct ndis_bssid_info bssid_info[1];
};

int add_wep_key(struct ndis_device *wnd, char *key, int key_len,
		int index);
int set_essid(struct ndis_device *wnd, const char *ssid, int ssid_len);
int set_infra_mode(struct ndis_device *wnd,
		   enum ndis_infrastructure_mode mode);
int get_ap_address(struct ndis_device *wnd, mac_address mac);
int set_ndis_auth_mode(struct ndis_device *wnd, ULONG auth_mode);
int set_iw_auth_mode(struct ndis_device *wnd, int wpa_version,
		     int auth_80211_alg);
int set_auth_mode(struct ndis_device *wnd);
int set_ndis_encr_mode(struct ndis_device *wnd, int cipher_pairwise,
		       int cipher_groupwise);
int get_ndis_encr_mode(struct ndis_device *wnd);
int set_encr_mode(struct ndis_device *wnd);
int set_iw_encr_mode(struct ndis_device *wnd, int cipher_pairwise,
		     int cipher_groupwise);
int get_ndis_auth_mode(struct ndis_device *wnd);
int set_priv_filter(struct ndis_device *wnd);
int set_scan(struct ndis_device *wnd);
NDIS_STATUS disassociate(struct ndis_device *wnd, int reset_ssid);
void set_default_iw_params(struct ndis_device *wnd);
extern const struct iw_handler_def ndis_handler_def;

#define PRIV_RESET	 		SIOCIWFIRSTPRIV+16
#define PRIV_POWER_PROFILE	 	SIOCIWFIRSTPRIV+17
#define PRIV_NETWORK_TYPE	 	SIOCIWFIRSTPRIV+18
#define PRIV_DEAUTHENTICATE 		SIOCIWFIRSTPRIV+19
#define PRIV_MEDIA_STREAM_MODE 		SIOCIWFIRSTPRIV+20
#define PRIV_RELOAD_DEFAULTS		SIOCIWFIRSTPRIV+23

#define RSN_INFO_ELEM		0x30

/* these have to match what is in wpa_supplicant */

typedef enum { WPA_ALG_NONE, WPA_ALG_WEP, WPA_ALG_TKIP, WPA_ALG_CCMP } wpa_alg;
typedef enum { CIPHER_NONE, CIPHER_WEP40, CIPHER_TKIP, CIPHER_CCMP,
	       CIPHER_WEP104 } wpa_cipher;
typedef enum { KEY_MGMT_802_1X, KEY_MGMT_PSK, KEY_MGMT_NONE,
	       KEY_MGMT_802_1X_NO_WPA, KEY_MGMT_WPA_NONE } wpa_key_mgmt;

#endif // IW_NDIS_H
