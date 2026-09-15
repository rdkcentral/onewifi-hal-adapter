#ifndef _RDK_HAL_ADAPTER_H_
#define _RDK_HAL_ADAPTER_H_

/*
 * Copyright 2025 Comcast Cable Communications Management, LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define NVRAM_NAME_SIZE 32

#define WPS_PIN_SIZE 9

#define PMODE_NONE 0x00
#define PMODE_A 0x01
#define PMODE_B 0x02
#define PMODE_G 0x04
#define PMODE_N 0x08
#define PMODE_AC 0x10
#define PMODE_AX 0x20

#define CHMODE_NONE PMODE_NONE
#define CHMODE_11A PMODE_A
#define CHMODE_11B PMODE_B
#define CHMODE_11G PMODE_G
#define CHMODE_11N PMODE_N
#define CHMODE_11AC PMODE_AC
#define CHMODE_11AX PMODE_AX

typedef enum {
    WIFI_HAL_LOG_LVL_DEBUG,
    WIFI_HAL_LOG_LVL_INFO,
    WIFI_HAL_LOG_LVL_ERROR,
    WIFI_HAL_LOG_LVL_MAX
} wifi_hal_log_level_t;

// wifi_halstats
typedef enum {
    WIFI_HAL_STATS_LOG_LVL_DEBUG,
    WIFI_HAL_STATS_LOG_LVL_INFO,
    WIFI_HAL_STATS_LOG_LVL_ERROR,
    WIFI_HAL_STATS_LOG_LVL_MAX
} wifi_hal_stats_log_level_t;

typedef struct {
    ULONG center;
    int bw;
    int members[8];
    int count;
} wifi_chan_block_t;

typedef struct wifi_enum_to_str_map {
    int enum_val;
    const char *str_val;
} wifi_enum_to_str_map_t;

struct wifiCountryEnumStrMap {
    wifi_countrycode_type_t countryCode;
    char countryStr[4];
};

typedef struct {
    wifi_countrycode_type_t cc;
} wifi_country_radio_op_class_t;

typedef wifi_vap_name_t wifi_vap_type_t;

typedef struct wifi_interface_info_t {
    char name[32];
    char bridge[32];
    unsigned int index;
    unsigned int phy_index;
    unsigned int rdk_radio_index;
    mac_address_t mac;
} wifi_interface_info_t;

typedef struct {
    char name[32];
    unsigned int index;
    unsigned int rdk_radio_index;
    wifi_radio_capabilities_t capab;
    wifi_radio_operationParam_t oper_param;
} wifi_radio_info_t;

typedef struct {
    unsigned int num_radios;
    wifi_radio_info_t radio_info[MAX_NUM_RADIOS];
} wifi_hal_priv_t;

typedef struct {
    CHAR interface_name[8];
    UINT vap_index;
    CHAR vap_name[32];
    BOOL vap_enable;
    BOOL radio_enable;
    CHAR default_ssid[64];
    CHAR default_passphrase[64];
    INT  mgmt_frame_pwr_ctrl;
    CHAR secure_mode_akm[32];
    CHAR secure_mode_mfp[32];
    CHAR default_radius_key[64];
    CHAR default_country_code[8];
    CHAR default_wps_pin[32];
    CHAR default_wifi_pwd[64];
    CHAR default_factory_ssid[64];
} __attribute__((__packed__)) wifi_interface_default_val_map_t;

#endif
