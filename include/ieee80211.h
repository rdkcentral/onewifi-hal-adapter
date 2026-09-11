/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Atsushi Onoe
 * Copyright (c) 2002-2009 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef _NET80211_IEEE80211_H_
#define _NET80211_IEEE80211_H_
#include <sys/types.h>
/*
 * 802.11 protocol definitions.
 */

#define        IEEE80211_ADDR_LEN      6               /* size of 802.11 address */
/* is 802.11 address multicast/broadcast? */
#define        IEEE80211_IS_MULTICAST(_a)      (*(_a) & 0x01)

#ifdef _KERNEL
extern const uint8_t ieee80211broadcastaddr[];
#endif

typedef uint16_t ieee80211_seq;

#define IEEE80211_PLCP_SFD      0xF3A0 
#define IEEE80211_PLCP_SERVICE  0x00
#define IEEE80211_PLCP_SERVICE_LOCKED  0x04
#define IEEE80211_PLCL_SERVICE_PBCC    0x08
#define IEEE80211_PLCP_SERVICE_LENEXT5 0x20
#define IEEE80211_PLCP_SERVICE_LENEXT6 0x40
#define IEEE80211_PLCP_SERVICE_LENEXT7 0x80

/*
 * generic definitions for IEEE 802.11 frames
 */
struct ieee80211_frame {
       uint8_t         i_fc[2];
       uint8_t         i_dur[2];
       union {
		struct {
		       uint8_t         i_addr1[IEEE80211_ADDR_LEN];
		       uint8_t         i_addr2[IEEE80211_ADDR_LEN];
		       uint8_t         i_addr3[IEEE80211_ADDR_LEN];
		};
		u_int8_t    i_addr_all[3 * IEEE80211_ADDR_LEN];
       };
       uint8_t         i_seq[2];
       /* possibly followed by addr4[IEEE80211_ADDR_LEN]; */
       /* see below */
} __attribute__((packed));

struct ieee80211_qosframe {
       uint8_t         i_fc[2];
       uint8_t         i_dur[2];
       uint8_t         i_addr1[IEEE80211_ADDR_LEN];
       uint8_t         i_addr2[IEEE80211_ADDR_LEN];
       uint8_t         i_addr3[IEEE80211_ADDR_LEN];
       uint8_t         i_seq[2];
       uint8_t         i_qos[2];
       /* possibly followed by addr4[IEEE80211_ADDR_LEN]; */
       /* see below */
} __attribute__((packed));

struct ieee80211_qoscntl {
       uint8_t         i_qos[2];
};

struct ieee80211_frame_addr4 {
       uint8_t         i_fc[2];
       uint8_t         i_dur[2];
       uint8_t         i_addr1[IEEE80211_ADDR_LEN];
       uint8_t         i_addr2[IEEE80211_ADDR_LEN];
       uint8_t         i_addr3[IEEE80211_ADDR_LEN];
       uint8_t         i_seq[2];
       uint8_t         i_addr4[IEEE80211_ADDR_LEN];
} __attribute__((packed));


struct ieee80211_qosframe_addr4 {
       uint8_t         i_fc[2];
       uint8_t         i_dur[2];
       uint8_t         i_addr1[IEEE80211_ADDR_LEN];
       uint8_t         i_addr2[IEEE80211_ADDR_LEN];
       uint8_t         i_addr3[IEEE80211_ADDR_LEN];
       uint8_t         i_seq[2];
       uint8_t         i_addr4[IEEE80211_ADDR_LEN];
       uint8_t         i_qos[2];
} __attribute__((packed));

/*
 * WME/802.11e information element.
 */
struct ieee80211_wme_info {
       uint8_t         wme_id;         /* IEEE80211_ELEMID_VENDOR */
       uint8_t         wme_len;        /* length in bytes */
       uint8_t         wme_oui[3];     /* 0x00, 0x50, 0xf2 */
       uint8_t         wme_type;       /* OUI type */
       uint8_t         wme_subtype;    /* OUI subtype */
       uint8_t         wme_version;    /* spec revision */
       uint8_t         wme_info;       /* QoS info */
} __attribute__((packed));

/*
 * WME AC parameter field
 */
struct ieee80211_wme_acparams {
       uint8_t         acp_aci_aifsn;
       uint8_t         acp_logcwminmax;
       uint16_t        acp_txop;
} __attribute__((packed));

#define WME_NUM_AC             4       /* 4 AC categories */
#define        WME_NUM_TID             16      /* 16 tids */

#define WME_AC_TO_TID(_ac) (       \
       ((_ac) == WME_AC_VO) ? 6 : \
       ((_ac) == WME_AC_VI) ? 5 : \
       ((_ac) == WME_AC_BK) ? 1 : \
       0)

#define TID_TO_WME_AC(_tid) (      \
       ((_tid) == 0 || (_tid) == 3) ? WME_AC_BE : \
       ((_tid) < 3) ? WME_AC_BK : \
       ((_tid) < 6) ? WME_AC_VI : \
       WME_AC_VO)

/*
 * Eth Frame Header.
 */

#define ETH_P_PAE       0x888E          /* Port Access Entity (IEEE 802.1X) */
#define ETH_ALEN        6               /* Octets in one ethernet addr   */

#ifdef __bitwise
#undef __bitwise
#endif
#define __bitwise

typedef uint16_t __bitwise be16;

struct ethhdr {
        unsigned char   h_dest[ETH_ALEN];       /* destination eth addr */
        unsigned char   h_source[ETH_ALEN];     /* source ether addr    */
        be16            h_proto;                /* packet type ID field */
} __attribute__((packed));

#endif /* _NET80211_IEEE80211_H_ */
