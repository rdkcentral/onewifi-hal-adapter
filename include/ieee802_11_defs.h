/*
 * IEEE 802.11 Frame type definitions
 * Copyright (c) 2002-2019, Jouni Malinen <j@w1.fi>
 * Copyright (c) 2007-2008 Intel Corporation
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef IEEE802_11_DEFS_H
#define IEEE802_11_DEFS_H

#include <common.h>

/* IEEE 802.11 defines */

#define WLAN_FC_PVER		0x0003
#define WLAN_FC_TODS		0x0100
#define WLAN_FC_FROMDS		0x0200
#define WLAN_FC_MOREFRAG	0x0400
#define WLAN_FC_RETRY		0x0800
#define WLAN_FC_PWRMGT		0x1000
#define WLAN_FC_MOREDATA	0x2000
#define WLAN_FC_ISWEP		0x4000
#define WLAN_FC_ORDER		0x8000

#define WLAN_FC_GET_TYPE(fc)	(((fc) & 0x000c) >> 2)
#define WLAN_FC_GET_STYPE(fc)	(((fc) & 0x00f0) >> 4)

#define WLAN_INVALID_MGMT_SEQ   0xFFFF

#define WLAN_GET_SEQ_FRAG(seq) ((seq) & (BIT(3) | BIT(2) | BIT(1) | BIT(0)))
#define WLAN_GET_SEQ_SEQ(seq) \
	(((seq) & (~(BIT(3) | BIT(2) | BIT(1) | BIT(0)))) >> 4)

#define WLAN_FC_TYPE_MGMT		0
#define WLAN_FC_TYPE_CTRL		1
#define WLAN_FC_TYPE_DATA		2

/* management */
#define WLAN_FC_STYPE_ASSOC_REQ		0
#define WLAN_FC_STYPE_ASSOC_RESP	1
#define WLAN_FC_STYPE_REASSOC_REQ	2
#define WLAN_FC_STYPE_REASSOC_RESP	3
#define WLAN_FC_STYPE_PROBE_REQ		4
#define WLAN_FC_STYPE_PROBE_RESP	5
#define WLAN_FC_STYPE_BEACON		8
#define WLAN_FC_STYPE_ATIM		9
#define WLAN_FC_STYPE_DISASSOC		10
#define WLAN_FC_STYPE_AUTH		11
#define WLAN_FC_STYPE_DEAUTH		12
#define WLAN_FC_STYPE_ACTION		13

/* control */
#define WLAN_FC_STYPE_PSPOLL		10
#define WLAN_FC_STYPE_RTS		11
#define WLAN_FC_STYPE_CTS		12
#define WLAN_FC_STYPE_ACK		13
#define WLAN_FC_STYPE_CFEND		14
#define WLAN_FC_STYPE_CFENDACK		15

/* data */
#define WLAN_FC_STYPE_DATA		0
#define WLAN_FC_STYPE_DATA_CFACK	1
#define WLAN_FC_STYPE_DATA_CFPOLL	2
#define WLAN_FC_STYPE_DATA_CFACKPOLL	3
#define WLAN_FC_STYPE_NULLFUNC		4
#define WLAN_FC_STYPE_CFACK		5
#define WLAN_FC_STYPE_CFPOLL		6
#define WLAN_FC_STYPE_CFACKPOLL		7
#define WLAN_FC_STYPE_QOS_DATA		8
#define WLAN_FC_STYPE_QOS_DATA_CFACK	9
#define WLAN_FC_STYPE_QOS_DATA_CFPOLL	10
#define WLAN_FC_STYPE_QOS_DATA_CFACKPOLL	11
#define WLAN_FC_STYPE_QOS_NULL		12
#define WLAN_FC_STYPE_QOS_CFPOLL	14
#define WLAN_FC_STYPE_QOS_CFACKPOLL	15

/* Authentication algorithms */
#define WLAN_AUTH_OPEN			0
#define WLAN_AUTH_SHARED_KEY		1
#define WLAN_AUTH_FT			2
#define WLAN_AUTH_SAE			3
#define WLAN_AUTH_FILS_SK		4
#define WLAN_AUTH_FILS_SK_PFS		5
#define WLAN_AUTH_FILS_PK		6
#define WLAN_AUTH_LEAP			128

#define WLAN_AUTH_CHALLENGE_LEN 128

#define        WLAN_SA_QUERY_TR_ID_LEN 2

#if 0
#define WLAN_CAPABILITY_ESS BIT(0)
#define WLAN_CAPABILITY_IBSS BIT(1)
#define WLAN_CAPABILITY_CF_POLLABLE BIT(2)
#define WLAN_CAPABILITY_CF_POLL_REQUEST BIT(3)
#define WLAN_CAPABILITY_PRIVACY BIT(4)
#define WLAN_CAPABILITY_SHORT_PREAMBLE BIT(5)
#define WLAN_CAPABILITY_PBCC BIT(6)
#define WLAN_CAPABILITY_CHANNEL_AGILITY BIT(7)
#define WLAN_CAPABILITY_SPECTRUM_MGMT BIT(8)
#define WLAN_CAPABILITY_QOS BIT(9)
#define WLAN_CAPABILITY_SHORT_SLOT_TIME BIT(10)
#define WLAN_CAPABILITY_APSD BIT(11)
#define WLAN_CAPABILITY_RADIO_MEASUREMENT BIT(12)
#define WLAN_CAPABILITY_DSSS_OFDM BIT(13)
#define WLAN_CAPABILITY_DELAYED_BLOCK_ACK BIT(14)
#define WLAN_CAPABILITY_IMM_BLOCK_ACK BIT(15)
#endif
/* Status codes (IEEE Std 802.11-2016, 9.4.1.9, Table 9-46) */
#define WLAN_STATUS_SUCCESS 0
#define WLAN_STATUS_UNSPECIFIED_FAILURE 1
#define WLAN_STATUS_TDLS_WAKEUP_ALTERNATE 2
#define WLAN_STATUS_TDLS_WAKEUP_REJECT 3
#define WLAN_STATUS_SECURITY_DISABLED 5
#define WLAN_STATUS_UNACCEPTABLE_LIFETIME 6
#define WLAN_STATUS_NOT_IN_SAME_BSS 7
#define WLAN_STATUS_CAPS_UNSUPPORTED 10
#define WLAN_STATUS_REASSOC_NO_ASSOC 11
#define WLAN_STATUS_ASSOC_DENIED_UNSPEC 12
#define WLAN_STATUS_NOT_SUPPORTED_AUTH_ALG 13
#define WLAN_STATUS_UNKNOWN_AUTH_TRANSACTION 14
#define WLAN_STATUS_CHALLENGE_FAIL 15
#define WLAN_STATUS_AUTH_TIMEOUT 16
#define WLAN_STATUS_AP_UNABLE_TO_HANDLE_NEW_STA 17
#define WLAN_STATUS_ASSOC_DENIED_RATES 18
#define WLAN_STATUS_ASSOC_DENIED_NOSHORT 19
#define WLAN_STATUS_SPEC_MGMT_REQUIRED 22
#define WLAN_STATUS_PWR_CAPABILITY_NOT_VALID 23
#define WLAN_STATUS_SUPPORTED_CHANNEL_NOT_VALID 24
#define WLAN_STATUS_ASSOC_DENIED_NO_SHORT_SLOT_TIME 25
#define WLAN_STATUS_ASSOC_DENIED_NO_HT 27
#define WLAN_STATUS_R0KH_UNREACHABLE 28
#define WLAN_STATUS_ASSOC_DENIED_NO_PCO 29
#define WLAN_STATUS_ASSOC_REJECTED_TEMPORARILY 30
#define WLAN_STATUS_ROBUST_MGMT_FRAME_POLICY_VIOLATION 31
#define WLAN_STATUS_UNSPECIFIED_QOS_FAILURE 32
#define WLAN_STATUS_DENIED_INSUFFICIENT_BANDWIDTH 33
#define WLAN_STATUS_DENIED_POOR_CHANNEL_CONDITIONS 34
#define WLAN_STATUS_DENIED_QOS_NOT_SUPPORTED 35
#define WLAN_STATUS_REQUEST_DECLINED 37
#define WLAN_STATUS_INVALID_PARAMETERS 38
#define WLAN_STATUS_REJECTED_WITH_SUGGESTED_CHANGES 39
#define WLAN_STATUS_INVALID_IE 40
#define WLAN_STATUS_GROUP_CIPHER_NOT_VALID 41
#define WLAN_STATUS_PAIRWISE_CIPHER_NOT_VALID 42
#define WLAN_STATUS_AKMP_NOT_VALID 43
#define WLAN_STATUS_UNSUPPORTED_RSN_IE_VERSION 44
#define WLAN_STATUS_INVALID_RSN_IE_CAPAB 45
#define WLAN_STATUS_CIPHER_REJECTED_PER_POLICY 46
#define WLAN_STATUS_TS_NOT_CREATED 47
#define WLAN_STATUS_DIRECT_LINK_NOT_ALLOWED 48
#define WLAN_STATUS_DEST_STA_NOT_PRESENT 49
#define WLAN_STATUS_DEST_STA_NOT_QOS_STA 50
#define WLAN_STATUS_ASSOC_DENIED_LISTEN_INT_TOO_LARGE 51
#define WLAN_STATUS_INVALID_FT_ACTION_FRAME_COUNT 52
#define WLAN_STATUS_INVALID_PMKID 53
#define WLAN_STATUS_INVALID_MDIE 54
#define WLAN_STATUS_INVALID_FTIE 55
#define WLAN_STATUS_REQUESTED_TCLAS_NOT_SUPPORTED 56
#define WLAN_STATUS_INSUFFICIENT_TCLAS_PROCESSING_RESOURCES 57
#define WLAN_STATUS_TRY_ANOTHER_BSS 58
#define WLAN_STATUS_GAS_ADV_PROTO_NOT_SUPPORTED 59
#define WLAN_STATUS_NO_OUTSTANDING_GAS_REQ 60
#define WLAN_STATUS_GAS_RESP_NOT_RECEIVED 61
#define WLAN_STATUS_STA_TIMED_OUT_WAITING_FOR_GAS_RESP 62
#define WLAN_STATUS_GAS_RESP_LARGER_THAN_LIMIT 63
#define WLAN_STATUS_REQ_REFUSED_HOME 64
#define WLAN_STATUS_ADV_SRV_UNREACHABLE 65
#define WLAN_STATUS_REQ_REFUSED_SSPN 67
#define WLAN_STATUS_REQ_REFUSED_UNAUTH_ACCESS 68
#define WLAN_STATUS_INVALID_RSNIE 72
#define WLAN_STATUS_U_APSD_COEX_NOT_SUPPORTED 73
#define WLAN_STATUS_U_APSD_COEX_MODE_NOT_SUPPORTED 74
#define WLAN_STATUS_BAD_INTERVAL_WITH_U_APSD_COEX 75
#define WLAN_STATUS_ANTI_CLOGGING_TOKEN_REQ 76
#define WLAN_STATUS_FINITE_CYCLIC_GROUP_NOT_SUPPORTED 77
#define WLAN_STATUS_CANNOT_FIND_ALT_TBTT 78
#define WLAN_STATUS_TRANSMISSION_FAILURE 79
#define WLAN_STATUS_REQ_TCLAS_NOT_SUPPORTED 80
#define WLAN_STATUS_TCLAS_RESOURCES_EXCHAUSTED 81
#define WLAN_STATUS_REJECTED_WITH_SUGGESTED_BSS_TRANSITION 82
#define WLAN_STATUS_REJECT_WITH_SCHEDULE 83
#define WLAN_STATUS_REJECT_NO_WAKEUP_SPECIFIED 84
#define WLAN_STATUS_SUCCESS_POWER_SAVE_MODE 85
#define WLAN_STATUS_PENDING_ADMITTING_FST_SESSION 86
#define WLAN_STATUS_PERFORMING_FST_NOW 87
#define WLAN_STATUS_PENDING_GAP_IN_BA_WINDOW 88
#define WLAN_STATUS_REJECT_U_PID_SETTING 89
#define WLAN_STATUS_REFUSED_EXTERNAL_REASON 92
#define WLAN_STATUS_REFUSED_AP_OUT_OF_MEMORY 93
#define WLAN_STATUS_REJECTED_EMERGENCY_SERVICE_NOT_SUPPORTED 94
#define WLAN_STATUS_QUERY_RESP_OUTSTANDING 95
#define WLAN_STATUS_REJECT_DSE_BAND 96
#define WLAN_STATUS_TCLAS_PROCESSING_TERMINATED 97
#define WLAN_STATUS_TS_SCHEDULE_CONFLICT 98
#define WLAN_STATUS_DENIED_WITH_SUGGESTED_BAND_AND_CHANNEL 99
#define WLAN_STATUS_MCCAOP_RESERVATION_CONFLICT 100
#define WLAN_STATUS_MAF_LIMIT_EXCEEDED 101
#define WLAN_STATUS_MCCA_TRACK_LIMIT_EXCEEDED 102
#define WLAN_STATUS_DENIED_DUE_TO_SPECTRUM_MANAGEMENT 103
#define WLAN_STATUS_ASSOC_DENIED_NO_VHT 104
#define WLAN_STATUS_ENABLEMENT_DENIED 105
#define WLAN_STATUS_RESTRICTION_FROM_AUTHORIZED_GDB 106
#define WLAN_STATUS_AUTHORIZATION_DEENABLED 107
#define WLAN_STATUS_FILS_AUTHENTICATION_FAILURE 112
#define WLAN_STATUS_UNKNOWN_AUTHENTICATION_SERVER 113
#define WLAN_STATUS_UNKNOWN_PASSWORD_IDENTIFIER 123

/* Reason codes (IEEE Std 802.11-2016, 9.4.1.7, Table 9-45) */
#define WLAN_REASON_PREV_AUTH_NOT_VALID 2

#define WLAN_EID_RSN 48
/* Interworking element (IEEE 802.11u) - Access Network Options */
#define INTERWORKING_ANO_ACCESS_NETWORK_MASK 0x0f
#define INTERWORKING_ANO_INTERNET 0x10
#define INTERWORKING_ANO_ASRA 0x20
#define INTERWORKING_ANO_ESR 0x40
#define INTERWORKING_ANO_UESA 0x80

#define INTERWORKING_ANT_PRIVATE 0
#define INTERWORKING_ANT_PRIVATE_WITH_GUEST 1
#define INTERWORKING_ANT_CHARGEABLE_PUBLIC 2
#define INTERWORKING_ANT_FREE_PUBLIC 3
#define INTERWORKING_ANT_PERSONAL_DEVICE 4
#define INTERWORKING_ANT_EMERGENCY_SERVICES 5
#define INTERWORKING_ANT_TEST 6
#define INTERWORKING_ANT_WILDCARD 15

/* Advertisement Protocol ID definitions (IEEE Std 802.11-2016, Table 9-215) */
enum adv_proto_id {
	ACCESS_NETWORK_QUERY_PROTOCOL = 0,
	MIH_INFO_SERVICE = 1,
	MIH_CMD_AND_EVENT_DISCOVERY = 2,
	EMERGENCY_ALERT_SYSTEM = 3,
	REGISTERED_LOCATION_QUERY_PROTO = 4,
	ADV_PROTO_VENDOR_SPECIFIC = 221
};

/* Access Network Query Protocol info ID definitions (IEEE Std 802.11-2016,
 * Table 9-271; P802.11ai) */
#ifdef _MSC_VER
#pragma pack(push, 1)
#endif /* _MSC_VER */
struct ieee80211_hdr {
	le16 frame_control;
	le16 duration_id;
	u8 addr1[6];
	u8 addr2[6];
	u8 addr3[6];
	le16 seq_ctrl;
	/* followed by 'u8 addr4[6];' if ToDS and FromDS is set in data frame
	 */
} STRUCT_PACKED;
#define IEEE80211_HDRLEN (sizeof(struct ieee80211_hdr))

#define IEEE80211_DA_FROMDS addr1
#define IEEE80211_BSSID_FROMDS addr2
#define IEEE80211_SA_FROMDS addr3

struct ieee80211_mgmt {
	le16 frame_control;
	le16 duration;
	u8 da[6];
	u8 sa[6];
	u8 bssid[6];
	le16 seq_ctrl;
	union {
		struct {
			le16 auth_alg;
			le16 auth_transaction;
			le16 status_code;
			/* possibly followed by Challenge text */
			u8 variable[];
		} STRUCT_PACKED auth;
		struct {
			le16 reason_code;
			u8 variable[];
		} STRUCT_PACKED deauth;
		struct {
			le16 capab_info;
			le16 listen_interval;
			/* followed by SSID and Supported rates */
			u8 variable[];
		} STRUCT_PACKED assoc_req;
		struct {
			le16 capab_info;
			le16 status_code;
			le16 aid;
			/* followed by Supported rates */
			u8 variable[];
		} STRUCT_PACKED assoc_resp, reassoc_resp;
		struct {
			le16 capab_info;
			le16 listen_interval;
			u8 current_ap[6];
			/* followed by SSID and Supported rates */
			u8 variable[];
		} STRUCT_PACKED reassoc_req;
		struct {
			le16 reason_code;
			u8 variable[];
		} STRUCT_PACKED disassoc;
		struct {
			u8 timestamp[8];
			le16 beacon_int;
			le16 capab_info;
			/* followed by some of SSID, Supported rates,
			 * FH Params, DS Params, CF Params, IBSS Params, TIM */
			u8 variable[];
		} STRUCT_PACKED beacon;
		/* probe_req: only variable items: SSID, Supported rates */
		struct {
			u8 timestamp[8];
			le16 beacon_int;
			le16 capab_info;
			/* followed by some of SSID, Supported rates,
			 * FH Params, DS Params, CF Params, IBSS Params */
			u8 variable[];
		} STRUCT_PACKED probe_resp;
		struct {
			u8 category;
			union {
				struct {
					u8 action_code;
					u8 dialog_token;
					u8 status_code;
					u8 variable[];
				} STRUCT_PACKED wmm_action;
				struct{
					u8 action_code;
					u8 element_id;
					u8 length;
					u8 switch_mode;
					u8 new_chan;
					u8 switch_count;
				} STRUCT_PACKED chan_switch;
				struct {
					u8 action;
					u8 sta_addr[ETH_ALEN];
					u8 target_ap_addr[ETH_ALEN];
					u8 variable[]; /* FT Request */
				} STRUCT_PACKED ft_action_req;
				struct {
					u8 action;
					u8 sta_addr[ETH_ALEN];
					u8 target_ap_addr[ETH_ALEN];
					le16 status_code;
					u8 variable[]; /* FT Request */
				} STRUCT_PACKED ft_action_resp;
				struct {
					u8 action;
					u8 trans_id[WLAN_SA_QUERY_TR_ID_LEN];
					u8 variable[]; /* OCI element */
				} STRUCT_PACKED sa_query_req;
				struct {
					u8 action; /* */
					u8 trans_id[WLAN_SA_QUERY_TR_ID_LEN];
					u8 variable[]; /* OCI element */
				} STRUCT_PACKED sa_query_resp;
				struct {
					u8 action;
					u8 dialogtoken;
					u8 variable[];
				} STRUCT_PACKED wnm_sleep_req;
				struct {
					u8 action;
					u8 dialogtoken;
					le16 keydata_len;
					u8 variable[];
				} STRUCT_PACKED wnm_sleep_resp;
				struct {
					u8 action;
					u8 variable[];
				} STRUCT_PACKED public_action;
				struct {
					u8 action; /* 9 */
					u8 oui[3];
					/* Vendor-specific content */
					u8 variable[];
				} STRUCT_PACKED vs_public_action;
				struct {
					u8 action; /* 7 */
					u8 dialog_token;
					u8 req_mode;
					le16 disassoc_timer;
					u8 validity_interval;
					/* BSS Termination Duration (optional),
					 * Session Information URL (optional),
					 * BSS Transition Candidate List
					 * Entries */
					u8 variable[];
				} STRUCT_PACKED bss_tm_req;
				struct {
					u8 action; /* 8 */
					u8 dialog_token;
					u8 status_code;
					u8 bss_termination_delay;
					/* Target BSSID (optional),
					 * BSS Transition Candidate List
					 * Entries (optional) */
					u8 variable[];
				} STRUCT_PACKED bss_tm_resp;
				struct {
					u8 action; /* 6 */
					u8 dialog_token;
					u8 query_reason;
					/* BSS Transition Candidate List
					 * Entries (optional) */
					u8 variable[];
				} STRUCT_PACKED bss_tm_query;
				struct {
					u8 action; /* 11 */
					u8 dialog_token;
					u8 req_info;
				} STRUCT_PACKED coloc_intf_req;
				struct {
					u8 action; /* 12 */
					u8 dialog_token;
					u8 variable[];
				} STRUCT_PACKED coloc_intf_report;
				struct {
					u8 action; /* 15 */
					u8 variable[];
				} STRUCT_PACKED slf_prot_action;
				struct {
					u8 action;
					u8 variable[];
				} STRUCT_PACKED fst_action;
				struct {
					u8 action;
					u8 dialog_token;
					u8 variable[];
				} STRUCT_PACKED rrm;
			} u;
		} STRUCT_PACKED action;
	} u;
} STRUCT_PACKED;


/* Access Categories / ACI to AC coding */
enum wmm_ac {
	WMM_AC_BE = 0 /* Best Effort */,
	WMM_AC_BK = 1 /* Background */,
	WMM_AC_VI = 2 /* Video */,
	WMM_AC_VO = 3 /* Voice */,
	WMM_AC_NUM = 4
};

#endif /* IEEE802_11_DEFS_H */
