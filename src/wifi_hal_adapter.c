/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2025 Comcast
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

#include "wifi_hal.h"
#include "wifi_hal_adapter.h"
#include "wifi_hal_rdk_framework.h"
#include <assert.h>
#include <cjson/cJSON.h>
#include <endian.h>
#include <errno.h>
#include <linux/rtnetlink.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netpacket/packet.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#define SSID_STRING_LEN 64

#define CSI_MAX_DEVICES_BRCM 10
#define HA_STRING_LENGTH_16 16
#define HA_STRING_LENGTH_18 18
#define HA_STRING_LENGTH_32 32
#define HA_STRING_LENGTH_64 64
#define HA_STRING_LENGTH_128 128
#define HA_STRING_LENGTH_256 256
#define HA_STRING_LENGTH_512 512
#define HA_STRING_LENGTH_1024 1024
#define HA_STRING_LENGTH_2048 2048
#define BUFFER_LENGTH_WIFIDB 256
#define INVALID_KEY "12345678"
#define MAX_KEYPASSPHRASE_LEN 129

#define IP_ADDR_LEN 45
#define INET6_ADDRSTRLEN 46

#define HAL_RADIO_STARTING_INDEX 0
#define HAL_AP_STARTING_INDEX 0 /* ?? */
#define HAL_AP_NUM_APS_PER_RADIO 8

#define CSI_DELAY_PERIOD 100 /* CSI collection period: 100ms */

#define BUF_LEN_26 26
#define NEIGHBOR_SECURITY_MODE_MAX 8
#define MAX_OPERATING_BANDWIDTH 5

#define INTERFACE_MAP_JSON "/tmp/InterfaceMap.json"

#ifndef HALADAPTER_SECURE_DATA_STORE_FILE
#define HALADAPTER_SECURE_DATA_STORE_FILE "/tmp/OnewifiHalAdapterDataStore.json"
#endif

/* Hardcoding the CSA beacon count to be 5*/
#define DEFAULT_CSA_BEACON_COUNT 5

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define WIFI_MAC_ADDRESS_LENGTH 6
#define HAL_AP_IDX_TO_HAL_RADIO(apIdx) \
    ((apIdx < (2 * HAL_AP_NUM_APS_PER_RADIO)) ? (apIdx % 2) : (apIdx / HAL_AP_NUM_APS_PER_RADIO))
#define HAL_AP_IDX_TO_SSID_IDX(apIdx) \
    ((apIdx < (2 * HAL_AP_NUM_APS_PER_RADIO)) ? (apIdx / 2) : (apIdx % HAL_AP_NUM_APS_PER_RADIO))
#define WL_DRIVER_TO_AP_IDX(idx, subidx) \
    ((idx < 2) ? (idx + subidx * 2 + 1) : ((idx * HAL_AP_NUM_APS_PER_RADIO) + subidx + 1))
#define HAL_RADIO_IDX_TO_HAL_AP(radioIdx) \
    ((radioIdx < 2) ? radioIdx : (radioIdx * HAL_AP_NUM_APS_PER_RADIO))
#define HAL_VAP_AP_INDEX_NEXT(apIdx) ((apIdx < 16) ? (apIdx + 2) : (apIdx + 1))

typedef void (*wifi_chan_event_CB_t)(wifi_channel_change_event_t radio_channel_param);
static pthread_mutex_t g_chan_event_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_monitor_thread;
static bool g_monitor_thread_running = false;

static void wifi_hal_print(wifi_hal_log_level_t level, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

#define wifi_hal_dbg_print(format, ...) \
    wifi_hal_print(WIFI_HAL_LOG_LVL_DEBUG, format, ##__VA_ARGS__)
#define wifi_hal_info_print(format, ...) \
    wifi_hal_print(WIFI_HAL_LOG_LVL_INFO, format, ##__VA_ARGS__)
#define wifi_hal_error_print(format, ...) \
    wifi_hal_print(WIFI_HAL_LOG_LVL_ERROR, format, ##__VA_ARGS__)

static wifi_hal_priv_t g_wifi_hal;

wifi_interface_info_t wifi_interface_info;
wifi_device_callbacks_t g_callbacks;

static wifi_radio_operationParam_t g_radio_oper_param[MAX_NUM_RADIOS] = { 0 };

static const wifi_interface_name_idex_map_t *interface_index_map;

static unsigned int interface_index_map_size;

static const radio_interface_mapping_t *l_radio_interface_map;
static unsigned int l_radio_interface_map_size;

static const wifi_interface_default_val_map_t *iface_def_val_map;

int g_ifaceCount = 0;

static int json_parse_interface_config_def_values(char *json_data, size_t file_size);

static wifi_channelBandwidth_t get_bandwidth_enum_from_string(char *bw_str);

static struct wifi_enum_to_str_map wifi_bandwidth_infoTable[] = {
    /* bwShift  bwStr */
    { WIFI_CHANNELBANDWIDTH_20MHZ,    "20"   },
    { WIFI_CHANNELBANDWIDTH_40MHZ,    "40"   },
    { WIFI_CHANNELBANDWIDTH_80MHZ,    "80"   },
    { WIFI_CHANNELBANDWIDTH_160MHZ,   "160"  },
    { WIFI_CHANNELBANDWIDTH_80_80MHZ, "8080" },
    { 0,                              NULL   }
};

wifi_enum_to_str_map_t mfp_table[] = {
    { wifi_mfp_cfg_disabled, "Disabled" },
    { wifi_mfp_cfg_optional, "Optional" },
    { wifi_mfp_cfg_required, "Required" },
    { 0xff,                  NULL       }
};

#define OPER_STANDS_MASK                                                                           \
    (WIFI_80211_VARIANT_AX | WIFI_80211_VARIANT_AC | WIFI_80211_VARIANT_N | WIFI_80211_VARIANT_G | \
        WIFI_80211_VARIANT_B | WIFI_80211_VARIANT_A)

/* 802.11 standard to wifi_ieee80211Variant_t */
static struct wifi_enum_to_str_map std2ieee80211Variant_infoTable[] = {
    /* ivar operStd */
    { WIFI_80211_VARIANT_AX, "ax" },
    { WIFI_80211_VARIANT_AD, "ad" },
    { WIFI_80211_VARIANT_AC, "ac" },
    { WIFI_80211_VARIANT_H,  "h"  },
    { WIFI_80211_VARIANT_N,  "n"  },
    { WIFI_80211_VARIANT_G,  "g"  },
    { WIFI_80211_VARIANT_B,  "b"  },
    { WIFI_80211_VARIANT_A,  "a"  },
    { 0,                     NULL }
};

wifi_enum_to_str_map_t security_mode_table[] = {
    { wifi_security_mode_none,                "None"                     },
    { wifi_security_mode_wep_64,              "WEP-64"                   },
    { wifi_security_mode_wep_128,             "WEP-128"                  },
    { wifi_security_mode_wpa_personal,        "WPA-Personal"             },
    { wifi_security_mode_wpa2_personal,       "WPA2-Personal"            },
    { wifi_security_mode_wpa_wpa2_personal,   "WPA-WPA2-Personal"        },
    { wifi_security_mode_wpa_enterprise,      "WPA-Enterprise"           },
    { wifi_security_mode_wpa2_enterprise,     "WPA2-Enterprise"          },
    { wifi_security_mode_wpa_wpa2_enterprise, "WPA-WPA2-Enterprise"      },
    { wifi_security_mode_wpa3_personal,       "WPA3-Personal"            },
    { wifi_security_mode_wpa3_transition,     "WPA3-Personal-Transition" },
    { wifi_security_mode_wpa3_enterprise,     "WPA3-Enterprise"          },
    { 0xff,                                   NULL                       }
};

wifi_enum_to_str_map_t encryption_table[] = {
    { wifi_encryption_tkip,     "TKIPEncryption"       },
    { wifi_encryption_aes,      "AESEncryption"        },
    { wifi_encryption_aes_tkip, "TKIPandAESEncryption" },
    { 0xff,                     NULL                   }
};

/* refer to wifi_ap_OnBoardingMethods_t for the enum definitions */
static wifi_enum_to_str_map_t wps_config_method_table[] = {
    { WIFI_ONBOARDINGMETHODS_USBFLASHDRIVE,      "USBFlashDrive"      },
    { WIFI_ONBOARDINGMETHODS_ETHERNET,           "Ethernet"           },
    { WIFI_ONBOARDINGMETHODS_LABEL,              "Label"              },
    { WIFI_ONBOARDINGMETHODS_DISPLAY,            "Display"            },
    { WIFI_ONBOARDINGMETHODS_EXTERNALNFCTOKEN,   "ExternalNFCToken"   },
    { WIFI_ONBOARDINGMETHODS_INTEGRATEDNFCTOKEN, "IntegratedNFCToken" },
    { WIFI_ONBOARDINGMETHODS_NFCINTERFACE,       "NFCInterface"       },
    { WIFI_ONBOARDINGMETHODS_PUSHBUTTON,         "PushButton"         },
    { WIFI_ONBOARDINGMETHODS_PIN,                "Keypad"             },
    { WIFI_ONBOARDINGMETHODS_PHYSICALPUSHBUTTON, "PhysicalPushButton" },
    { WIFI_ONBOARDINGMETHODS_PHYSICALDISPLAY,    "PhysicalDisplay"    },
    { WIFI_ONBOARDINGMETHODS_VIRTUALPUSHBUTTON,  "VirtualPushButton"  },
    { WIFI_ONBOARDINGMETHODS_VIRTUALDISPLAY,     "VirtualDisplay"     },
    { WIFI_ONBOARDINGMETHODS_EASYCONNECT,        "EASYCONNECT"        }, // not expected in WPS APIs
    { 0xff,                                      NULL                 }
};

struct operStdPMode_info {
    char *operStd;
    char *pmodeStr;
    unsigned int pmodeVal;
};

/* operatingStandard to pureMode value for wifi_getRadioStandard */
static struct operStdPMode_info operStdPMode_infoTable[] = {
    /* operStd  pmodeStr    pmodeVal    Old RadioStandard Reference */
    { "b,g,n",     "n",  PMODE_NONE            }, /* gOnly = nOnly = acOnly = FALSE; */
    { "g,n",       "n",  PMODE_G               }, /* gOnly = TRUE; nOnly = acOnly = FALSE; */
    { "g",         "g",  PMODE_G               }, /* gOnly = TRUE; nOnly = acOnly = FALSE; */
    { "b,g",       "g",  PMODE_NONE            }, /* gOnly = nOnly = acOnly = FALSE; */
    { "n",         "n",  PMODE_N               }, /* nOnly = TRUE; gOnly = acOnly = FALSE; */
    { "a,n,ac",    "ac", PMODE_NONE            }, /* gOnly = nOnly = acOnly = FALSE; */
    { "n,ac",      "ac", PMODE_N               }, /* nOnly = TRUE; gOnly = acOnly = FALSE; */
    { "ac",        "ac", PMODE_AC              }, /* acOnly = TRUE; gOnly = nOnly = FALSE; */
    { "b,g,n,ax",  "ax", PMODE_NONE            }, /* gOnly = nOnly = acOnly = axOnly = FALSE; */
    { "g,n,ax",    "ax", (PMODE_AX | PMODE_G)  }, /* 2G */
    { "n,ax",      "ax", (PMODE_AX | PMODE_N)  }, /* 2G */
    { "ac,ax",     "ax", (PMODE_AX | PMODE_AC) }, /* 5G */
    { "n,ac,ax",   "ax", (PMODE_AX | PMODE_N)  }, /* 5G */
    { "a,n,ac,ax", "ax", PMODE_NONE            }, /* gOnly = nOnly = acOnly = axOnly = FALSE; */
    { "ax",        "ax", PMODE_AX              }, /* axOnly = TRUE; gOnly = nOnly = acOnly = FALSE; */
    { NULL,        NULL, PMODE_NONE            }
};

static int wl_map_pureMode_to_operatingStandard(unsigned int pMode, char *pModeStr,
    char *output_string)
{
    if (!output_string) {
        wifi_hal_error_print("%s: Invalid output_string pointer\n", __func__);
        return -1;
    }

    if (pMode == PMODE_NONE) {
        snprintf(output_string, HA_STRING_LENGTH_512, "b,g,n");
        wifi_hal_dbg_print("%s: pMode=0x%x (PMODE_NONE), operStd=b,g,n\n", __func__, pMode);
        return 0;
    }

    /* Search for exact match based on pmodeVal */
    for (int i = 0; operStdPMode_infoTable[i].operStd != NULL; i++) {
        if ((operStdPMode_infoTable[i].pmodeVal == pMode) &&
            (strcmp(operStdPMode_infoTable[i].pmodeStr, pModeStr) == 0)) {
            /* Exact match found */
            snprintf(output_string, HA_STRING_LENGTH_512, "%s", operStdPMode_infoTable[i].operStd);
            wifi_hal_dbg_print("%s: pMode=0x%x matches pmodeVal, operStd=%s, pmodeStr=%s\n",
                __func__, pMode, operStdPMode_infoTable[i].operStd,
                operStdPMode_infoTable[i].pmodeStr);
            return 0;
        }
    }

    wifi_hal_error_print("%s: Invalid pMode value 0x%x - no matching entry in table\n", __func__,
        pMode);
    return -1;
}

void wifi_hal_print(wifi_hal_log_level_t level, const char *format, ...)
{
    char buff[256] = { 0 };
    va_list list;
    FILE *fpg = NULL;

    get_formatted_time(buff);

#ifdef LINUX_VM_PORT
    printf("%s ", buff);
    va_start(list, format);
    vprintf(format, list);
    va_end(list);
#else
    if ((access("/nvram/wifiHalDbg", R_OK)) == 0) {

        fpg = fopen("/tmp/wifiHal", "a+");
        if (fpg == NULL) {
            return;
        }
    } else {
        switch (level) {
        case WIFI_HAL_LOG_LVL_INFO:
        case WIFI_HAL_LOG_LVL_ERROR:
            fpg = fopen("/rdklogs/logs/wifiHal.txt", "a+");
            if (fpg == NULL) {
                return;
            }
            break;
        case WIFI_HAL_LOG_LVL_DEBUG:
        default:
            return;
        }
    }
    static const char *level_marker[WIFI_HAL_LOG_LVL_MAX] = {
        [WIFI_HAL_LOG_LVL_DEBUG] = "<D>",
        [WIFI_HAL_LOG_LVL_INFO] = "<I>",
        [WIFI_HAL_LOG_LVL_ERROR] = "<E>",
    };
    if (level < WIFI_HAL_LOG_LVL_MAX)
        snprintf(&buff[strlen(buff)], 256 - strlen(buff), " %s ", level_marker[level]);

    fprintf(fpg, "%s ", buff);
    va_start(list, format);
    vfprintf(fpg, format, list);
    va_end(list);
    fflush(fpg);
    fclose(fpg);
#endif
    return;
}

wifi_enum_to_str_map_t countrycode_table[] = {
    { wifi_countrycode_AC,  "AC" }, /**< ASCENSION ISLAND */
    { wifi_countrycode_AD,  "AD" }, /**< ANDORRA */
    { wifi_countrycode_AE,  "AE" }, /**< UNITED ARAB EMIRATES */
    { wifi_countrycode_AF,  "AF" }, /**< AFGHANISTAN */
    { wifi_countrycode_AG,  "AG" }, /**< ANTIGUA AND BARBUDA */
    { wifi_countrycode_AI,  "AI" }, /**< ANGUILLA */
    { wifi_countrycode_AL,  "AL" }, /**< ALBANIA */
    { wifi_countrycode_AM,  "AM" }, /**< ARMENIA */
    { wifi_countrycode_AN,  "AN" }, /**< NETHERLANDS ANTILLES */
    { wifi_countrycode_AO,  "AO" }, /**< ANGOLA */
    { wifi_countrycode_AQ,  "AQ" }, /**< ANTARCTICA */
    { wifi_countrycode_AR,  "AR" }, /**< ARGENTINA */
    { wifi_countrycode_AS,  "AS" }, /**< AMERICAN SAMOA */
    { wifi_countrycode_AT,  "AT" }, /**< AUSTRIA */
    { wifi_countrycode_AU,  "AU" }, /**< AUSTRALIA */
    { wifi_countrycode_AW,  "AW" }, /**< ARUBA */
    { wifi_countrycode_AZ,  "AZ" }, /**< AZERBAIJAN */
    { wifi_countrycode_BA,  "BA" }, /**< BOSNIA AND HERZEGOVINA */
    { wifi_countrycode_BB,  "BB" }, /**< BARBADOS */
    { wifi_countrycode_BD,  "BD" }, /**< BANGLADESH */
    { wifi_countrycode_BE,  "BE" }, /**< BELGIUM */
    { wifi_countrycode_BF,  "BF" }, /**< BURKINA FASO */
    { wifi_countrycode_BG,  "BG" }, /**< BULGARIA */
    { wifi_countrycode_BH,  "BH" }, /**< BAHRAIN */
    { wifi_countrycode_BI,  "BI" }, /**< BURUNDI */
    { wifi_countrycode_BJ,  "BJ" }, /**< BENIN */
    { wifi_countrycode_BM,  "BM" }, /**< BERMUDA */
    { wifi_countrycode_BN,  "BN" }, /**< BRUNEI DARUSSALAM */
    { wifi_countrycode_BO,  "BO" }, /**< BOLIVIA */
    { wifi_countrycode_BR,  "BR" }, /**< BRAZIL */
    { wifi_countrycode_BS,  "BS" }, /**< BAHAMAS */
    { wifi_countrycode_BT,  "BT" }, /**< BHUTAN */
    { wifi_countrycode_BV,  "BV" }, /**< BOUVET ISLAND */
    { wifi_countrycode_BW,  "BW" }, /**< BOTSWANA */
    { wifi_countrycode_BY,  "BY" }, /**< BELARUS */
    { wifi_countrycode_BZ,  "BZ" }, /**< BELIZE */
    { wifi_countrycode_CA,  "CA" }, /**< CANADA */
    { wifi_countrycode_CC,  "CC" }, /**< COCOS (KEELING) ISLANDS */
    { wifi_countrycode_CD,  "CD" }, /**< CONGO, THE DEMOCRATIC REPUBLIC OF THE */
    { wifi_countrycode_CF,  "CF" }, /**< CENTRAL AFRICAN REPUBLIC */
    { wifi_countrycode_CG,  "CG" }, /**< CONGO */
    { wifi_countrycode_CH,  "CH" }, /**< SWITZERLAND */
    { wifi_countrycode_CI,  "CI" }, /**< COTE D'IVOIRE */
    { wifi_countrycode_CK,  "CK" }, /**< COOK ISLANDS */
    { wifi_countrycode_CL,  "CL" }, /**< CHILE */
    { wifi_countrycode_CM,  "CM" }, /**< CAMEROON */
    { wifi_countrycode_CN,  "CN" }, /**< CHINA */
    { wifi_countrycode_CO,  "CO" }, /**< COLOMBIA */
    { wifi_countrycode_CP,  "CP" }, /**< CLIPPERTON ISLAND */
    { wifi_countrycode_CR,  "CR" }, /**< COSTA RICA */
    { wifi_countrycode_CU,  "CU" }, /**< CUBA */
    { wifi_countrycode_CV,  "CV" }, /**< CAPE VERDE */
    { wifi_countrycode_CY,  "CY" }, /**< CYPRUS */
    { wifi_countrycode_CX,  "CX" }, /**< CHRISTMAS ISLAND */
    { wifi_countrycode_CZ,  "CZ" }, /**< CZECH REPUBLIC */
    { wifi_countrycode_DE,  "DE" }, /**< GERMANY */
    { wifi_countrycode_DJ,  "DJ" }, /**< DJIBOUTI */
    { wifi_countrycode_DK,  "DK" }, /**< DENMARK */
    { wifi_countrycode_DM,  "DM" }, /**< DOMINICA */
    { wifi_countrycode_DO,  "DO" }, /**< DOMINICAN REPUBLIC */
    { wifi_countrycode_DZ,  "DZ" }, /**< ALGERIA */
    { wifi_countrycode_EC,  "EC" }, /**< ECUADOR */
    { wifi_countrycode_EE,  "EE" }, /**< ESTONIA */
    { wifi_countrycode_EG,  "EG" }, /**< EGYPT */
    { wifi_countrycode_EH,  "EH" }, /**< WESTERN SAHARA */
    { wifi_countrycode_ER,  "ER" }, /**< ERITREA */
    { wifi_countrycode_ES,  "ES" }, /**< SPAIN */
    { wifi_countrycode_ET,  "ET" }, /**< ETHIOPIA */
    { wifi_countrycode_FI,  "FI" }, /**< FINLAND */
    { wifi_countrycode_FJ,  "FJ" }, /**< FIJI */
    { wifi_countrycode_FK,  "FK" }, /**< FALKLAND ISLANDS (MALVINAS) */
    { wifi_countrycode_FM,  "FM" }, /**< MICRONESIA, FEDERATED STATES OF */
    { wifi_countrycode_FO,  "FO" }, /**< FAROE ISLANDS */
    { wifi_countrycode_FR,  "FR" }, /**< FRANCE */
    { wifi_countrycode_GA,  "GA" }, /**< GABON */
    { wifi_countrycode_GB,  "GB" }, /**< UNITED KINGDOM */
    { wifi_countrycode_GD,  "GD" }, /**< GRENADA */
    { wifi_countrycode_GE,  "GE" }, /**< GEORGIA */
    { wifi_countrycode_GF,  "GF" }, /**< FRENCH GUIANA */
    { wifi_countrycode_GG,  "GG" }, /**< GUERNSEY */
    { wifi_countrycode_GH,  "GH" }, /**< GHANA */
    { wifi_countrycode_GI,  "GI" }, /**< GIBRALTAR */
    { wifi_countrycode_GL,  "GL" }, /**< GREENLAND */
    { wifi_countrycode_GM,  "GM" }, /**< GAMBIA */
    { wifi_countrycode_GN,  "GN" }, /**< GUINEA */
    { wifi_countrycode_GP,  "GP" }, /**< GUADELOUPE */
    { wifi_countrycode_GQ,  "GQ" }, /**< EQUATORIAL GUINEA */
    { wifi_countrycode_GR,  "GR" }, /**< GREECE */
    { wifi_countrycode_GS,  "GS" }, /**< SOUTH GEORGIA AND THE SOUTH SANDWICH ISLANDS */
    { wifi_countrycode_GT,  "GT" }, /**< GUATEMALA */
    { wifi_countrycode_GU,  "GU" }, /**< GUAM */
    { wifi_countrycode_GW,  "GW" }, /**< GUINEA-BISSAU */
    { wifi_countrycode_GY,  "GY" }, /**< GUYANA */
    { wifi_countrycode_HR,  "HR" }, /**< CROATIA */
    { wifi_countrycode_HT,  "HT" }, /**< HAITI */
    { wifi_countrycode_HM,  "HM" }, /**< HEARD ISLAND AND MCDONALD ISLANDS */
    { wifi_countrycode_HN,  "HN" }, /**< HONDURAS */
    { wifi_countrycode_HK,  "HK" }, /**< HONG KONG */
    { wifi_countrycode_HU,  "HU" }, /**< HUNGARY */
    { wifi_countrycode_IS,  "IS" }, /**< ICELAND */
    { wifi_countrycode_IN,  "IN" }, /**< INDIA */
    { wifi_countrycode_ID,  "ID" }, /**< INDONESIA */
    { wifi_countrycode_IR,  "IR" }, /**< IRAN, ISLAMIC REPUBLIC OF */
    { wifi_countrycode_IQ,  "IQ" }, /**< IRAQ */
    { wifi_countrycode_IE,  "IE" }, /**< IRELAND */
    { wifi_countrycode_IL,  "IL" }, /**< ISRAEL */
    { wifi_countrycode_IM,  "IM" }, /**< MAN, ISLE OF */
    { wifi_countrycode_IT,  "IT" }, /**< ITALY */
    { wifi_countrycode_IO,  "IO" }, /**< BRITISH INDIAN OCEAN TERRITORY */
    { wifi_countrycode_JM,  "JM" }, /**< JAMAICA */
    { wifi_countrycode_JP,  "JP" }, /**< JAPAN */
    { wifi_countrycode_JE,  "JE" }, /**< JERSEY */
    { wifi_countrycode_JO,  "JO" }, /**< JORDAN */
    { wifi_countrycode_KE,  "KE" }, /**< KENYA */
    { wifi_countrycode_KG,  "KG" }, /**< KYRGYZSTAN */
    { wifi_countrycode_KH,  "KH" }, /**< CAMBODIA */
    { wifi_countrycode_KI,  "KI" }, /**< KIRIBATI */
    { wifi_countrycode_KM,  "KM" }, /**< COMOROS */
    { wifi_countrycode_KN,  "KN" }, /**< SAINT KITTS AND NEVIS */
    { wifi_countrycode_KP,  "KP" }, /**< KOREA, DEMOCRATIC PEOPLE'S REPUBLIC OF */
    { wifi_countrycode_KR,  "KR" }, /**< KOREA, REPUBLIC OF */
    { wifi_countrycode_KW,  "KW" }, /**< KUWAIT */
    { wifi_countrycode_KY,  "KY" }, /**< CAYMAN ISLANDS */
    { wifi_countrycode_KZ,  "KZ" }, /**< KAZAKHSTAN */
    { wifi_countrycode_LA,  "LA" }, /**< LAO PEOPLE'S DEMOCRATIC REPUBLIC */
    { wifi_countrycode_LB,  "LB" }, /**< LEBANON */
    { wifi_countrycode_LC,  "LC" }, /**< SAINT LUCIA */
    { wifi_countrycode_LI,  "LI" }, /**< LIECHTENSTEIN */
    { wifi_countrycode_LK,  "LK" }, /**< SRI LANKA */
    { wifi_countrycode_LR,  "LR" }, /**< LIBERIA */
    { wifi_countrycode_LS,  "LS" }, /**< LESOTHO */
    { wifi_countrycode_LT,  "LT" }, /**< LITHUANIA */
    { wifi_countrycode_LU,  "LU" }, /**< LUXEMBOURG */
    { wifi_countrycode_LV,  "LV" }, /**< LATVIA */
    { wifi_countrycode_LY,  "LY" }, /**< LIBYAN ARAB JAMAHIRIYA */
    { wifi_countrycode_MA,  "MA" }, /**< MOROCCO */
    { wifi_countrycode_MC,  "MC" }, /**< MONACO */
    { wifi_countrycode_MD,  "MD" }, /**< MOLDOVA, REPUBLIC OF */
    { wifi_countrycode_ME,  "ME" }, /**< MONTENEGRO */
    { wifi_countrycode_MG,  "MG" }, /**< MADAGASCAR */
    { wifi_countrycode_MH,  "MH" }, /**< MARSHALL ISLANDS */
    { wifi_countrycode_MK,  "MK" }, /**< MACEDONIA, THE FORMER YUGOSLAV REPUBLIC OF */
    { wifi_countrycode_ML,  "ML" }, /**< MALI */
    { wifi_countrycode_MM,  "MM" }, /**< MYANMAR */
    { wifi_countrycode_MN,  "MN" }, /**< MONGOLIA */
    { wifi_countrycode_MO,  "MO" }, /**< MACAO */
    { wifi_countrycode_MQ,  "MQ" }, /**< MARTINIQUE */
    { wifi_countrycode_MR,  "MR" }, /**< MAURITANIA */
    { wifi_countrycode_MS,  "MS" }, /**< MONTSERRAT */
    { wifi_countrycode_MT,  "MT" }, /**< MALTA */
    { wifi_countrycode_MU,  "MU" }, /**< MAURITIUS */
    { wifi_countrycode_MV,  "MV" }, /**< MALDIVES */
    { wifi_countrycode_MW,  "MW" }, /**< MALAWI */
    { wifi_countrycode_MX,  "MX" }, /**< MEXICO */
    { wifi_countrycode_MY,  "MY" }, /**< MALAYSIA */
    { wifi_countrycode_MZ,  "MZ" }, /**< MOZAMBIQUE */
    { wifi_countrycode_NA,  "NA" }, /**< NAMIBIA */
    { wifi_countrycode_NC,  "NC" }, /**< NEW CALEDONIA */
    { wifi_countrycode_NE,  "NE" }, /**< NIGER */
    { wifi_countrycode_NF,  "NF" }, /**< NORFOLK ISLAND */
    { wifi_countrycode_NG,  "NG" }, /**< NIGERIA */
    { wifi_countrycode_NI,  "NI" }, /**< NICARAGUA */
    { wifi_countrycode_NL,  "NL" }, /**< NETHERLANDS */
    { wifi_countrycode_NO,  "NO" }, /**< NORWAY */
    { wifi_countrycode_NP,  "NP" }, /**< NEPAL */
    { wifi_countrycode_NR,  "NR" }, /**< NAURU */
    { wifi_countrycode_NU,  "NU" }, /**< NIUE */
    { wifi_countrycode_NZ,  "NZ" }, /**< NEW ZEALAND */
    { wifi_countrycode_MP,  "MP" }, /**< NORTHERN MARIANA ISLANDS */
    { wifi_countrycode_OM,  "OM" }, /**< OMAN */
    { wifi_countrycode_PA,  "PA" }, /**< PANAMA */
    { wifi_countrycode_PE,  "PE" }, /**< PERU */
    { wifi_countrycode_PF,  "PF" }, /**< FRENCH POLYNESIA */
    { wifi_countrycode_PG,  "PG" }, /**< PAPUA NEW GUINEA */
    { wifi_countrycode_PH,  "PH" }, /**< PHILIPPINES */
    { wifi_countrycode_PK,  "PK" }, /**< PAKISTAN */
    { wifi_countrycode_PL,  "PL" }, /**< POLAND */
    { wifi_countrycode_PM,  "PM" }, /**< SAINT PIERRE AND MIQUELON */
    { wifi_countrycode_PN,  "PN" }, /**< PITCAIRN */
    { wifi_countrycode_PR,  "PR" }, /**< PUERTO RICO */
    { wifi_countrycode_PS,  "PS" }, /**< PALESTINIAN TERRITORY, OCCUPIED */
    { wifi_countrycode_PT,  "PT" }, /**< PORTUGAL */
    { wifi_countrycode_PW,  "PW" }, /**< PALAU */
    { wifi_countrycode_PY,  "PY" }, /**< PARAGUAY */
    { wifi_countrycode_QA,  "QA" }, /**< QATAR */
    { wifi_countrycode_RE,  "RE" }, /**< REUNION */
    { wifi_countrycode_RO,  "RO" }, /**< ROMANIA */
    { wifi_countrycode_RS,  "RS" }, /**< SERBIA */
    { wifi_countrycode_RU,  "RU" }, /**< RUSSIAN FEDERATION */
    { wifi_countrycode_RW,  "RW" }, /**< RWANDA */
    { wifi_countrycode_SA,  "SA" }, /**< SAUDI ARABIA */
    { wifi_countrycode_SB,  "SB" }, /**< SOLOMON ISLANDS */
    { wifi_countrycode_SD,  "SD" }, /**< SUDAN */
    { wifi_countrycode_SE,  "SE" }, /**< SWEDEN */
    { wifi_countrycode_SC,  "SC" }, /**< SEYCHELLES */
    { wifi_countrycode_SG,  "SG" }, /**< SINGAPORE */
    { wifi_countrycode_SH,  "SH" }, /**< SAINT HELENA */
    { wifi_countrycode_SI,  "SI" }, /**< SLOVENIA */
    { wifi_countrycode_SJ,  "SJ" }, /**< SVALBARD AND JAN MAYEN */
    { wifi_countrycode_SK,  "SK" }, /**< SLOVAKIA */
    { wifi_countrycode_SL,  "SL" }, /**< SIERRA LEONE */
    { wifi_countrycode_SM,  "SM" }, /**< SAN MARINO */
    { wifi_countrycode_SN,  "SN" }, /**< SENEGAL */
    { wifi_countrycode_SO,  "SO" }, /**< SOMALIA */
    { wifi_countrycode_SR,  "SR" }, /**< SURINAME */
    { wifi_countrycode_ST,  "ST" }, /**< SAO TOME AND PRINCIPE */
    { wifi_countrycode_SV,  "SV" }, /**< EL SALVADOR */
    { wifi_countrycode_SY,  "SY" }, /**< SYRIAN ARAB REPUBLIC */
    { wifi_countrycode_SZ,  "SZ" }, /**< SWAZILAND */
    { wifi_countrycode_TA,  "TA" }, /**< TRISTAN DA CUNHA */
    { wifi_countrycode_TC,  "TC" }, /**< TURKS AND CAICOS ISLANDS */
    { wifi_countrycode_TD,  "TD" }, /**< CHAD */
    { wifi_countrycode_TF,  "TF" }, /**< FRENCH SOUTHERN TERRITORIES */
    { wifi_countrycode_TG,  "TG" }, /**< TOGO */
    { wifi_countrycode_TH,  "TH" }, /**< THAILAND */
    { wifi_countrycode_TJ,  "TJ" }, /**< TAJIKISTAN */
    { wifi_countrycode_TK,  "TK" }, /**< TOKELAU */
    { wifi_countrycode_TL,  "TL" }, /**< TIMOR-LESTE (EAST TIMOR) */
    { wifi_countrycode_TM,  "TM" }, /**< TURKMENISTAN */
    { wifi_countrycode_TN,  "TN" }, /**< TUNISIA */
    { wifi_countrycode_TO,  "TO" }, /**< TONGA */
    { wifi_countrycode_TR,  "TR" }, /**< TURKEY */
    { wifi_countrycode_TT,  "TT" }, /**< TRINIDAD AND TOBAGO */
    { wifi_countrycode_TV,  "TV" }, /**< TUVALU */
    { wifi_countrycode_TW,  "TW" }, /**< TAIWAN, PROVINCE OF CHINA */
    { wifi_countrycode_TZ,  "TZ" }, /**< TANZANIA, UNITED REPUBLIC OF */
    { wifi_countrycode_UA,  "UA" }, /**< UKRAINE */
    { wifi_countrycode_UG,  "UG" }, /**< UGANDA */
    { wifi_countrycode_UM,  "UM" }, /**< UNITED STATES MINOR OUTLYING ISLANDS */
    { wifi_countrycode_US,  "US" }, /**< UNITED STATES */
    { wifi_countrycode_UY,  "UY" }, /**< URUGUAY */
    { wifi_countrycode_UZ,  "UZ" }, /**< UZBEKISTAN */
    { wifi_countrycode_VA,  "VA" }, /**< HOLY SEE (VATICAN CITY STATE) */
    { wifi_countrycode_VC,  "VC" }, /**< SAINT VINCENT AND THE GRENADINES */
    { wifi_countrycode_VE,  "VE" }, /**< VENEZUELA */
    { wifi_countrycode_VG,  "VG" }, /**< VIRGIN ISLANDS, BRITISH */
    { wifi_countrycode_VI,  "VI" }, /**< VIRGIN ISLANDS, U.S. */
    { wifi_countrycode_VN,  "VN" }, /**< VIET NAM */
    { wifi_countrycode_VU,  "VU" }, /**< VANUATU */
    { wifi_countrycode_WF,  "WF" }, /**< WALLIS AND FUTUNA */
    { wifi_countrycode_WS,  "WS" }, /**< SAMOA */
    { wifi_countrycode_YE,  "YE" }, /**< YEMEN */
    { wifi_countrycode_YT,  "YT" }, /**< MAYOTTE */
    { wifi_countrycode_YU,  "YU" }, /**< YUGOSLAVIA */
    { wifi_countrycode_ZA,  "ZA" }, /**< SOUTH AFRICA */
    { wifi_countrycode_ZM,  "ZM" }, /**< ZAMBIA */
    { wifi_countrycode_ZW,  "ZW" }, /**< ZIMBABWE */
    { wifi_countrycode_max, NULL }, /**< Max number of country code */
};

static void str_to_upper(char *str)
{
    char *p = str;

    while (*p) {
        *p = toupper(*p);
        p++;
    }
}

static int init_json_nvram_def_values(void)
{
    FILE *fp = fopen(HALADAPTER_SECURE_DATA_STORE_FILE, "r");
    if (!fp) {
        wifi_hal_error_print("Failed to open JSON file %s %d\n", __func__, __LINE__);
        return -1;
    }

    // Determine file size
    if (fseek(fp, 0, SEEK_END) != 0) {
        wifi_hal_error_print("fseek failed %s %d\n", __func__, __LINE__);
        fclose(fp);
        return -1;
    }

    long file_size = ftell(fp);
    if (file_size < 0) {
        wifi_hal_error_print("Invalid file size %s %d\n", __func__, __LINE__);
        fclose(fp);
        return -1;
    }
    rewind(fp);

    size_t f_size = (size_t)file_size;

    char *json_data = malloc(f_size + 1);
    if (!json_data) {
        wifi_hal_error_print("malloc failed %s %d", __func__, __LINE__);
        fclose(fp);
        return -1;
    }

    size_t read_size = fread(json_data, 1, f_size, fp);
    fclose(fp);

    if (read_size != (size_t)f_size) {
        wifi_hal_error_print("Failed to read the whole interface config file %s %d\n", __func__,
            __LINE__);
        free(json_data);
        return -1;
    }

    json_data[f_size] = '\0'; // Null-terminate

    int ret = json_parse_interface_config_def_values(json_data, f_size);

    free(json_data);

    if (ret == RETURN_ERR) {
        wifi_hal_error_print("JSON parser failure for interface config file %s %d\n", __func__,
            __LINE__);
        return -1;
    }

    return 0;
}

static int json_parse_interface_config_def_values(char *json_data, size_t file_size)
{
    wifi_interface_default_val_map_t *tmp_iface_def_val_map = NULL;

    cJSON *root = cJSON_ParseWithLength(json_data, file_size);
    if (!root) {
        wifi_hal_error_print("JSON parse error: %s %s %d\n", cJSON_GetErrorPtr(), __func__,
            __LINE__);
        return RETURN_ERR;
    }

    cJSON *ifaceList = cJSON_GetObjectItem(root, "InterfaceList");
    if (!cJSON_IsArray(ifaceList)) {
        wifi_hal_error_print("Invalid or missing InterfaceList %s %d\n", __func__, __LINE__);
        cJSON_Delete(root);
        return RETURN_ERR;
    }

    int tmp_ifaceCount = cJSON_GetArraySize(ifaceList);
    wifi_hal_info_print("Total Interfaces: %d %s %d\n", tmp_ifaceCount, __func__, __LINE__);

    if (tmp_ifaceCount <= 0) {
        wifi_hal_error_print("%s:%d No interfaces found\n", __func__, __LINE__);
        cJSON_Delete(root);
        return RETURN_ERR;
    }

    // Allocate memory dynamically for interfaces
    tmp_iface_def_val_map = malloc(tmp_ifaceCount * sizeof(wifi_interface_default_val_map_t));
    if (!tmp_iface_def_val_map) {
        wifi_hal_error_print("%s:%d Memory allocation failed\n", __func__, __LINE__);
        cJSON_Delete(root);
        return RETURN_ERR;
    }

    memset(tmp_iface_def_val_map, 0, tmp_ifaceCount * sizeof(wifi_interface_default_val_map_t));

    for (int i = 0; i < tmp_ifaceCount; i++) {
        cJSON *iface = cJSON_GetArrayItem(ifaceList, i);
        if (!cJSON_IsObject(iface))
            continue;

        cJSON *ifname = cJSON_GetObjectItem(iface, "InterfaceName");
        snprintf(tmp_iface_def_val_map[i].interface_name,
            sizeof(tmp_iface_def_val_map[i].interface_name) - 1, "%s",
            cJSON_IsString(ifname) ? ifname->valuestring : "");

        cJSON *vapIndex = cJSON_GetObjectItem(iface, "VapIndex");
        tmp_iface_def_val_map[i].vap_index = cJSON_IsNumber(vapIndex) ? vapIndex->valueint : 0;

        cJSON *vapName = cJSON_GetObjectItem(iface, "VapName");
        snprintf(tmp_iface_def_val_map[i].vap_name, sizeof(tmp_iface_def_val_map[i].vap_name) - 1,
            "%s", cJSON_IsString(vapName) ? vapName->valuestring : "");

        cJSON *vapEnable = cJSON_GetObjectItem(iface, "VapEnable");
        tmp_iface_def_val_map[i].vap_enable = cJSON_IsString(vapEnable) ?
            atoi(vapEnable->valuestring) :
            0;

        cJSON *radioEnable = cJSON_GetObjectItem(iface, "RadioEnable");
        tmp_iface_def_val_map[i].radio_enable = cJSON_IsString(radioEnable) ?
            atoi(radioEnable->valuestring) :
            0;

        cJSON *ssid = cJSON_GetObjectItem(iface, "DefaultSSID");
        snprintf(tmp_iface_def_val_map[i].default_ssid,
            sizeof(tmp_iface_def_val_map[i].default_ssid) - 1, "%s",
            cJSON_IsString(ssid) ? ssid->valuestring : "");

        cJSON *pass = cJSON_GetObjectItem(iface, "DefaultPASSPHRASE");
        snprintf(tmp_iface_def_val_map[i].default_passphrase,
            sizeof(tmp_iface_def_val_map[i].default_passphrase) - 1, "%s",
            cJSON_IsString(pass) ? pass->valuestring : "");

        cJSON *mgmt = cJSON_GetObjectItem(iface, "MgmtFramePowerControl");
        tmp_iface_def_val_map[i].mgmt_frame_pwr_ctrl = cJSON_IsNumber(mgmt) ? mgmt->valueint : 0;

        cJSON *akm = cJSON_GetObjectItem(iface, "SecureModeAkm");
        snprintf(tmp_iface_def_val_map[i].secure_mode_akm,
            sizeof(tmp_iface_def_val_map[i].secure_mode_akm) - 1, "%s",
            cJSON_IsString(akm) ? akm->valuestring : "");

        cJSON *mfp = cJSON_GetObjectItem(iface, "SecureModeMfp");
        snprintf(tmp_iface_def_val_map[i].secure_mode_mfp,
            sizeof(tmp_iface_def_val_map[i].secure_mode_mfp) - 1, "%s",
            cJSON_IsString(mfp) ? mfp->valuestring : "");

        cJSON *radiuskey = cJSON_GetObjectItem(iface, "DefaultRadiusKey");
        snprintf(tmp_iface_def_val_map[i].default_radius_key,
            sizeof(tmp_iface_def_val_map[i].default_radius_key) - 1, "%s",
            cJSON_IsString(radiuskey) ? radiuskey->valuestring : "");

        cJSON *countrycode = cJSON_GetObjectItem(iface, "DefaultCountryCode");
        snprintf(tmp_iface_def_val_map[i].default_country_code,
            sizeof(tmp_iface_def_val_map[i].default_country_code) - 1, "%s",
            cJSON_IsString(countrycode) ? countrycode->valuestring : "");

        cJSON *wpspin = cJSON_GetObjectItem(iface, "DefaultWpsPin");
        snprintf(tmp_iface_def_val_map[i].default_wps_pin,
            sizeof(tmp_iface_def_val_map[i].default_wps_pin) - 1, "%s",
            cJSON_IsString(wpspin) ? wpspin->valuestring : "");

        cJSON *wifipw = cJSON_GetObjectItem(iface, "DefaultWifiPwd");
        snprintf(tmp_iface_def_val_map[i].default_wifi_pwd,
            sizeof(tmp_iface_def_val_map[i].default_wifi_pwd) - 1, "%s",
            cJSON_IsString(wifipw) ? wifipw->valuestring : "");

        cJSON *factoryssid = cJSON_GetObjectItem(iface, "DefaultFactorySSID");
        snprintf(tmp_iface_def_val_map[i].default_factory_ssid,
            sizeof(tmp_iface_def_val_map[i].default_factory_ssid) - 1, "%s",
            cJSON_IsString(factoryssid) ? factoryssid->valuestring : "");
    }

    if (iface_def_val_map != NULL) {
        free(iface_def_val_map);
        iface_def_val_map = NULL;
    }

    iface_def_val_map = tmp_iface_def_val_map;

    g_ifaceCount = tmp_ifaceCount;

    for (int i = 0; i < g_ifaceCount; i++) {
        memcpy(&iface_def_val_map[i], &tmp_iface_def_val_map[i],
            sizeof(wifi_interface_default_val_map_t));

        wifi_hal_info_print("Interface %d:\n", i);
        wifi_hal_info_print("  InterfaceName           : %s\n",
            iface_def_val_map[i].interface_name);
        wifi_hal_info_print("  VapIndex                : %u\n", iface_def_val_map[i].vap_index);
        wifi_hal_info_print("  VapName                 : %s\n", iface_def_val_map[i].vap_name);
        wifi_hal_info_print("  VapEnable               : %d\n", iface_def_val_map[i].vap_enable);
        wifi_hal_info_print("  RadioEnable             : %d\n", iface_def_val_map[i].radio_enable);
        wifi_hal_info_print("  DefaultSSID             : %s\n", iface_def_val_map[i].default_ssid);
        wifi_hal_info_print("  DefaultPASSPHRASE       : %s\n",
            iface_def_val_map[i].default_passphrase);
        wifi_hal_info_print("  MgmtFramePowerControl   : %d\n",
            iface_def_val_map[i].mgmt_frame_pwr_ctrl);
        wifi_hal_info_print("  SecureModeAkm           : %s\n",
            iface_def_val_map[i].secure_mode_akm);
        wifi_hal_info_print("  SecureModeMfp           : %s\n",
            iface_def_val_map[i].secure_mode_mfp);
        wifi_hal_info_print("  DefaultRadiusKey        : %s\n",
            iface_def_val_map[i].default_radius_key);
        wifi_hal_info_print("  DefaultCountryCode      : %s\n",
            iface_def_val_map[i].default_country_code);
        wifi_hal_info_print("  DefaultWpsPin           : %s\n",
            iface_def_val_map[i].default_wps_pin);
        wifi_hal_info_print("  DefaultWifiPwd          : %s\n",
            iface_def_val_map[i].default_wifi_pwd);
        wifi_hal_info_print("  DefaultFactorySSID      : %s\n",
            iface_def_val_map[i].default_factory_ssid);
    }

    tmp_iface_def_val_map = NULL;

    cJSON_Delete(root);
    return 0;
}

static inline cJSON *json_open_interface_map(FILE *fp, size_t len)
{
    cJSON *json;
    char *buff;

    buff = malloc(len);
    if (buff == NULL) {
        wifi_hal_error_print("%s:%d: Failed to allocate %zu bytes for json file\n", __func__,
            __LINE__, len);
        return NULL;
    }

    len = fread(buff, 1, len, fp);

    json = cJSON_ParseWithLength(buff, len);
    if (json == NULL) {
        const char *const error_ptr = cJSON_GetErrorPtr();
        wifi_hal_error_print("%s:%d: Error json file parse: %s\n", __func__, __LINE__,
            (error_ptr ? error_ptr : "UNKNOWN"));
    }

    free(buff);

    return json;
}

static inline int json_parse_interface_map(cJSON *json)
{
    cJSON *phy_list;
    cJSON *phy_index;
    cJSON *phy_elm;
    cJSON *radio_list;
    cJSON *radio_elm;
    cJSON *radio_index;
    cJSON *radio_name;
    cJSON *inteface_list;
    cJSON *interface_elm;
    cJSON *interface_name;
    cJSON *mld_interface_name;
    cJSON *bridge;
    cJSON *vlan_id;
    cJSON *vap_index;
    cJSON *vap_name;
    wifi_interface_name_idex_map_t *tmp_intf_idx_map;
    radio_interface_mapping_t *tmp_radio_interface_map;
    unsigned int radio_interface_map_size;
    unsigned int interface_idx_map_size;
    unsigned int r_idx;
    unsigned int i_idx;
    cJSON_bool valid;

    phy_list = cJSON_GetObjectItem(json, "PhyList");
    if (!cJSON_IsArray(phy_list)) {
        wifi_hal_error_print("%s:%d: [PhyList] does not exist or is not an array\n", __func__,
            __LINE__);
        return -1;
    }

    radio_interface_map_size = 0;
    interface_idx_map_size = 0;

    cJSON_ArrayForEach(phy_elm, phy_list) {
        phy_index = cJSON_GetObjectItem(phy_elm, "Index");
        if (!(valid = cJSON_IsNumber(phy_index))) {
            wifi_hal_error_print("%s:%d: (Index) does not exist or is not a number\n", __func__,
                __LINE__);
            break;
        }

        radio_list = cJSON_GetObjectItem(phy_elm, "RadioList");
        if (!(valid = cJSON_IsArray(radio_list))) {
            wifi_hal_error_print("%s:%d: [RadioList] does not exist or is not an array\n", __func__,
                __LINE__);
            break;
        }

        cJSON_ArrayForEach(radio_elm, radio_list) {
            radio_index = cJSON_GetObjectItem(radio_elm, "Index");
            if (!(valid = cJSON_IsNumber(radio_index))) {
                wifi_hal_error_print("%s:%d: (Index) does not exist "
                                     "or is not a number\n",
                    __func__, __LINE__);
                break;
            }

            radio_name = cJSON_GetObjectItem(radio_elm, "RadioName");
            if (!(valid = cJSON_IsString(radio_name))) {
                wifi_hal_error_print("%s:%d: (RadioName) does not exist "
                                     "or is not a string\n",
                    __func__, __LINE__);
                break;
            }

            inteface_list = cJSON_GetObjectItem(radio_elm, "InterfaceList");
            if (!(valid = cJSON_IsArray(inteface_list))) {
                wifi_hal_error_print("%s:%d: [InterfaceList] does "
                                     "not exist or is not an array\n",
                    __func__, __LINE__);
                break;
            }

            cJSON_ArrayForEach(interface_elm, inteface_list) {
                interface_name = cJSON_GetObjectItem(interface_elm, "InterfaceName");
                if (!(valid = cJSON_IsString(interface_name))) {
                    wifi_hal_error_print("%s:%d: (InterfaceName) does "
                                         "not exist or is not a string\n",
                        __func__, __LINE__);
                    break;
                }

                bridge = cJSON_GetObjectItem(interface_elm, "Bridge");
                if (!(valid = cJSON_IsString(bridge))) {
                    wifi_hal_error_print("%s:%d: (Bridge) does "
                                         "not exist or is not a string\n",
                        __func__, __LINE__);
                    break;
                }

                vlan_id = cJSON_GetObjectItem(interface_elm, "vlanId");
                if (!(valid = cJSON_IsNumber(vlan_id))) {
                    wifi_hal_error_print("%s:%d: (vlanId) does "
                                         "not exist or is not a number\n",
                        __func__, __LINE__);
                    break;
                }

                vap_index = cJSON_GetObjectItem(interface_elm, "VapIndex");
                if (!(valid = cJSON_IsNumber(vap_index))) {
                    wifi_hal_error_print("%s:%d: (vapIndex) does "
                                         "not exist or is not a number\n",
                        __func__, __LINE__);
                    break;
                }

                vap_name = cJSON_GetObjectItem(interface_elm, "VapName");
                if (!(valid = cJSON_IsString(vap_name))) {
                    wifi_hal_error_print("%s:%d: (vapName) does "
                                         "not exist or is not a string\n",
                        __func__, __LINE__);
                    break;
                }
                interface_idx_map_size++;
            }
            if (!valid) {
                wifi_hal_error_print("%s:%d: Failed to [InterfaceList] validation\n", __func__,
                    __LINE__);
                break;
            }
            radio_interface_map_size++;
        }
        if (!valid) {
            wifi_hal_error_print("%s:%d: Failed to [RadioList] validation\n", __func__, __LINE__);
            break;
        }
    }
    if (!valid) {
        wifi_hal_error_print("%s:%d: Failed to [PhyList] validation\n", __func__, __LINE__);
        return -1;
    }

    tmp_intf_idx_map = NULL;
    tmp_radio_interface_map = NULL;

    if (!((tmp_intf_idx_map = malloc(sizeof(*tmp_intf_idx_map) * interface_idx_map_size)) &&
            (tmp_radio_interface_map = malloc(
                 sizeof(*tmp_radio_interface_map) * radio_interface_map_size)))) {
        wifi_hal_error_print("%s:%d: Failed to allocate interface_idx_map(%d - %u "
                             "bytes) or radio_interface_map_size(%d - %u bytes)\n",
            __func__, __LINE__, !!tmp_intf_idx_map, interface_idx_map_size,
            !!tmp_radio_interface_map, radio_interface_map_size);

        free(tmp_radio_interface_map);
        free(tmp_intf_idx_map);

        return -1;
    }

    // filling occurs from the end
    i_idx = interface_idx_map_size - 1;
    r_idx = radio_interface_map_size - 1;

    cJSON_ArrayForEach(phy_elm, phy_list) {
        phy_index = cJSON_GetObjectItem(phy_elm, "Index");
        radio_list = cJSON_GetObjectItem(phy_elm, "RadioList");

        cJSON_ArrayForEach(radio_elm, radio_list) {
            radio_index = cJSON_GetObjectItem(radio_elm, "Index");
            radio_name = cJSON_GetObjectItem(radio_elm, "RadioName");
            inteface_list = cJSON_GetObjectItem(radio_elm, "InterfaceList");

            tmp_radio_interface_map[r_idx].phy_index = (unsigned int)cJSON_GetNumberValue(
                phy_index);
            tmp_radio_interface_map[r_idx].radio_index = (unsigned int)cJSON_GetNumberValue(
                radio_index);

            snprintf(tmp_radio_interface_map[r_idx].radio_name,
                sizeof(tmp_radio_interface_map[r_idx].radio_name), "radio%u",
                tmp_radio_interface_map[r_idx].radio_index + 1);

            strncpy(tmp_radio_interface_map[r_idx].interface_name, cJSON_GetStringValue(radio_name),
                (sizeof(tmp_radio_interface_map[r_idx].interface_name) /
                    sizeof(*tmp_radio_interface_map[r_idx].interface_name)) -
                    1);
            tmp_radio_interface_map[r_idx]
                .interface_name[(sizeof(tmp_radio_interface_map[r_idx].interface_name) /
                                    sizeof(*tmp_radio_interface_map[r_idx].interface_name)) -
                    1] = '\0';

            cJSON_ArrayForEach(interface_elm, inteface_list) {
                interface_name = cJSON_GetObjectItem(interface_elm, "InterfaceName");
                mld_interface_name = cJSON_GetObjectItem(interface_elm, "MldName");
                bridge = cJSON_GetObjectItem(interface_elm, "Bridge");
                vlan_id = cJSON_GetObjectItem(interface_elm, "vlanId");
                vap_index = cJSON_GetObjectItem(interface_elm, "vapIndex");
                vap_name = cJSON_GetObjectItem(interface_elm, "vapName");

                tmp_intf_idx_map[i_idx].phy_index = tmp_radio_interface_map[r_idx].phy_index;

                tmp_intf_idx_map[i_idx].rdk_radio_index =
                    tmp_radio_interface_map[r_idx].radio_index;

                strncpy(tmp_intf_idx_map[i_idx].interface_name,
                    cJSON_GetStringValue(interface_name),
                    (sizeof(tmp_intf_idx_map[i_idx].interface_name) /
                        sizeof(*tmp_intf_idx_map[i_idx].interface_name)) -
                        1);
                tmp_intf_idx_map[i_idx]
                    .interface_name[(sizeof(tmp_intf_idx_map[i_idx].interface_name) /
                                        sizeof(*tmp_intf_idx_map[i_idx].interface_name)) -
                        1] = '\0';

                if (mld_interface_name != NULL && cJSON_IsString(mld_interface_name)) {
                    strncpy(tmp_intf_idx_map[i_idx].mld_interface_name,
                        cJSON_GetStringValue(mld_interface_name),
                        sizeof(tmp_intf_idx_map[i_idx].mld_interface_name) - 1);
                }

                strncpy(tmp_intf_idx_map[i_idx].bridge_name, cJSON_GetStringValue(bridge),
                    (sizeof(tmp_intf_idx_map[i_idx].bridge_name) /
                        sizeof(*tmp_intf_idx_map[i_idx].bridge_name)) -
                        1);
                tmp_intf_idx_map[i_idx]
                    .bridge_name[(sizeof(tmp_intf_idx_map[i_idx].bridge_name) /
                                     sizeof(*tmp_intf_idx_map[i_idx].bridge_name)) -
                        1] = '\0';

                tmp_intf_idx_map[i_idx].vlan_id = (unsigned int)cJSON_GetNumberValue(vlan_id);

                tmp_intf_idx_map[i_idx].index = (unsigned int)cJSON_GetNumberValue(vap_index);

                strncpy(tmp_intf_idx_map[i_idx].vap_name, cJSON_GetStringValue(vap_name),
                    (sizeof(tmp_intf_idx_map[i_idx].vap_name) /
                        sizeof(*tmp_intf_idx_map[i_idx].vap_name)) -
                        1);
                tmp_intf_idx_map[i_idx].vap_name[(sizeof(tmp_intf_idx_map[i_idx].vap_name) /
                                                     sizeof(*tmp_intf_idx_map[i_idx].vap_name)) -
                    1] = '\0';
                i_idx--;
            }
            r_idx--;
        }
    }
    interface_index_map = tmp_intf_idx_map;
    interface_index_map_size = interface_idx_map_size;

    l_radio_interface_map = tmp_radio_interface_map;
    l_radio_interface_map_size = radio_interface_map_size;

    return 0;
}

static inline int init_json_interface_map(void)
{
    FILE *fp;
    cJSON *json;
    long len;
    int ret;

    fp = fopen(INTERFACE_MAP_JSON, "r");
    if (fp == NULL) {
        wifi_hal_error_print("%s:%d: Failed (err=%d, msg=%s) to opening interface map file:%s\n",
            __func__, __LINE__, errno, strerror(errno), INTERFACE_MAP_JSON);
        return -1;
    }

    ret = -1;

    fseek(fp, 0, SEEK_END);
    len = ftell(fp);
    if (len < 0) {
        fclose(fp);
        return ret;
    }
    fseek(fp, 0, SEEK_SET);

    json = json_open_interface_map(fp, (size_t)len);
    if (json) {
        ret = json_parse_interface_map(json);

        cJSON_Delete(json);
    }

    fclose(fp);

    return ret;
}

static unsigned int get_sizeof_interfaces_index_map(void)
{
    return interface_index_map_size;
}

static unsigned int get_sizeof_radio_interfaces_map(void)
{
    return l_radio_interface_map_size;
}

static void get_radio_interface_info_map(radio_interface_mapping_t *radio_interface_map)
{
    memcpy(radio_interface_map, l_radio_interface_map,
        get_sizeof_radio_interfaces_map() * sizeof(radio_interface_mapping_t));
}

static void get_wifi_interface_info_map(wifi_interface_name_idex_map_t *interface_map)
{
    memcpy(interface_map, interface_index_map,
        get_sizeof_interfaces_index_map() * sizeof(wifi_interface_name_idex_map_t));
}

static void init_interface_map(void)
{
    unsigned int i;
    int json_ret = 0;

    json_ret = init_json_interface_map();
    if (json_ret < 0) {
        wifi_hal_error_print("%s:%d: Json parsing intetface failure\n", __func__, __LINE__);
        return;
    }

    wifi_hal_info_print("%s:%d: Using JSON Interface Map\n", __func__, __LINE__);

    json_ret = 0;

    json_ret = init_json_nvram_def_values();

    if (json_ret < 0) {
        wifi_hal_error_print("%s:%d: Json parsing nvram def values failure\n", __func__, __LINE__);
        return;
    }

    wifi_hal_info_print("%s:%d: Interface Index Map(%u):\n", __func__, __LINE__,
        interface_index_map_size);
    for (i = 0; i < interface_index_map_size; i++) {
        wifi_hal_info_print("\t[%u]={phy_index:%u, rdk_radio_index:%u, interface_name:%s, "
                            "bridge_name:%s, vlan_id:%d, index:%u, vap_name:%s}\n",
            i, interface_index_map[i].phy_index, interface_index_map[i].rdk_radio_index,
            interface_index_map[i].interface_name, interface_index_map[i].bridge_name,
            interface_index_map[i].vlan_id, interface_index_map[i].index,
            interface_index_map[i].vap_name);
    }

    wifi_hal_info_print("%s:%d: Radio Interface Index Map(%u):\n", __func__, __LINE__,
        l_radio_interface_map_size);
    for (i = 0; i < l_radio_interface_map_size; i++) {
        wifi_hal_info_print("\t[%u]={phy_index:%u, radio_index:%u, radio_name:%s, "
                            "interface_name:%s}\n",
            i, l_radio_interface_map[i].phy_index, l_radio_interface_map[i].radio_index,
            l_radio_interface_map[i].radio_name, l_radio_interface_map[i].interface_name);
    }
}

static int get_wifi_interface_name_from_vap_index(unsigned int vap_index, char *interface_name)
{
    // OneWifi interafce mapping with vap_index
    unsigned char l_index = 0;
    unsigned char total_num_of_vaps = 0;
    const char *l_interface_name = NULL;
    wifi_radio_info_t *radio;
    for (l_index = 0; l_index < g_wifi_hal.num_radios; l_index++) {
        radio = &g_wifi_hal.radio_info[l_index];
        total_num_of_vaps += radio->capab.maxNumberVAPs;
    }
    wifi_hal_error_print("%s:%d: total_num_of_vaps:%d radio_num:%d\n", __func__, __LINE__,
        total_num_of_vaps, g_wifi_hal.num_radios);
    if ((vap_index >= total_num_of_vaps) || (interface_name == NULL)) {
        wifi_hal_error_print("%s:%d: Wrong vap_index:%d \n", __func__, __LINE__, vap_index);
        return RETURN_ERR;
    } else {
        wifi_hal_dbg_print("%s:%d:  vap_index:%d \n", __func__, __LINE__, vap_index);
    }

    for (l_index = 0; l_index < get_sizeof_interfaces_index_map(); l_index++) {
        if (interface_index_map[l_index].index == vap_index) {
            l_interface_name = interface_index_map[l_index].interface_name;
            strncpy(interface_name, l_interface_name, (strlen(l_interface_name) + 1));
            wifi_hal_dbg_print("%s:%d: VAP index %d: interface name %s\n", __func__, __LINE__,
                vap_index, interface_name);
            return RETURN_OK;
        }
    }

    wifi_hal_error_print("%s:%d: Interface name not found:%d \n", __func__, __LINE__, vap_index);

    return RETURN_ERR;
}

static int wl_str2uintArray(char *istr, char *delim, unsigned int *count, unsigned int *uarray,
    unsigned int maxCount)
{
    char *tmp_str, *tok, *rest;
    unsigned int i = 0;

    if ((istr == NULL) || (delim == NULL) || (count == NULL) || (uarray == NULL)) {
        return RETURN_ERR;
    }
    tmp_str = strdup(istr);
    if (tmp_str == NULL) {
        return RETURN_ERR;
    }
    tok = strtok_r(tmp_str, delim, &rest);
    while (tok && (i < maxCount)) {
        uarray[i] = atoi(tok);
        i++;
        tok = strtok_r(NULL, delim, &rest);
    }
    free(tmp_str);
    *count = i;
    return RETURN_OK;
}

static int get_security_mode_int_from_str(char *security_mode_str, char *mfp_str,
    wifi_security_modes_t *security_mode)
{

    if (strcmp(security_mode_str, "None") == 0) {
        *security_mode = wifi_security_mode_none;
    } else if (strcmp(security_mode_str, "owe") == 0) {
        *security_mode = wifi_security_mode_enhanced_open;
    } else if (strcmp(security_mode_str, "psk") == 0) {
        *security_mode = wifi_security_mode_wpa_personal;
    } else if (strcmp(security_mode_str, "psk2") == 0) {
        *security_mode = wifi_security_mode_wpa2_personal;
    } else if (strcmp(security_mode_str, "psk psk2") == 0) {
        *security_mode = wifi_security_mode_wpa_wpa2_personal;
    } else if ((strstr(security_mode_str, "sae") != NULL) &&
        (strstr(security_mode_str, "psk2") == NULL)) {
        /* should also take care of "sae sae-ext" case regardless of order */
        *security_mode = wifi_security_mode_wpa3_personal;
    } else if (strstr(security_mode_str, "psk2") && strstr(security_mode_str, "sae")) {
        /* should also take care of "psk2 sae sae-ext" case regardless of order */
        *security_mode = wifi_security_mode_wpa3_transition;
    } else if (strcmp(security_mode_str, "wpa") == 0) {
        *security_mode = wifi_security_mode_wpa_enterprise;
    } else if ((strcmp(security_mode_str, "wpa2") == 0) && (strcmp(mfp_str, "2") != 0)) {
        *security_mode = wifi_security_mode_wpa2_enterprise;
    } else if ((strcmp(security_mode_str, "wpa2") == 0) && (strcmp(mfp_str, "2") == 0)) {
        *security_mode = wifi_security_mode_wpa3_enterprise;
    } else if (strcmp(security_mode_str, "wpa wpa2") == 0) {
        *security_mode = wifi_security_mode_wpa_wpa2_enterprise;
    } else if (strstr(security_mode_str, "psk2") && strstr(security_mode_str, "sae") &&
        !strcmp(mfp_str, "0")) {
        *security_mode = wifi_security_mode_wpa3_compatibility;
    } else {
        wifi_hal_error_print("%s:%d: wifi security mode not found:[%s:%s]\r\n", __func__, __LINE__,
            security_mode_str, mfp_str);
        return RETURN_ERR;
    }

    wifi_hal_dbg_print("%s:%d: security mode %d string %s and mfp is %s\r\n", __func__, __LINE__,
        *security_mode, security_mode_str, mfp_str);
    return RETURN_OK;
}

static int wl_getRadioCapabilities(int radioIndex, wifi_radio_capabilities_t *rcap)
{
    char xpwrSupStr[HA_STRING_LENGTH_256], encModeStr[HA_STRING_LENGTH_256], *abbrev,
        cbuf[HA_STRING_LENGTH_1024];
    int i, j, ccnt, ret, numbands, bandis2g, bcnt, acnt;
    int idx = 0;
    char pModeStr[HA_STRING_LENGTH_64] = { 0 };
    char output_string[HA_STRING_LENGTH_512] = { 0 };
    uint32_t possibleChannels[MAX_CHANNELS + 1] = { 0 };
    wifi_channels_list_t *chlistp, *bchlistp, *achlistp;
    wifi_channelBandwidth_t *chbwp;
    UINT pMode = 0;
    wifi_enum_to_str_map_t *cctentry;
	BOOL gOnly, nOnly, acOnly;
    int channelCount = 0;
    wifi_channelMap_t radio_channels[HA_STRING_LENGTH_128];

    /* numSupportedFreqBand - if dual band is 2G and 5G capable */
    rcap->numSupportedFreqBand = 1;
    numbands = rcap->numSupportedFreqBand;
    wifi_freq_bands_t band = 0;


    memset(radio_channels, 0, sizeof(radio_channels));
    memset(possibleChannels, 0, sizeof(possibleChannels));

    ret = wifi_getRadioChannels(radioIndex, radio_channels, ARRAY_SIZE(radio_channels));
    if (ret < 0) {
        wifi_hal_error_print("%s:%d: wifi_getRadioChannels failed for radio %d\n",
                              __func__, __LINE__, radioIndex);
        return RETURN_ERR;
    }

    for (idx = 0; idx < (int)ARRAY_SIZE(radio_channels); idx++)
    {
        if (radio_channels[idx].ch_number){
            possibleChannels[channelCount++] = radio_channels[idx].ch_number;
        }
    }

    ret = wifi_getRadioFrequencyBand(radioIndex, &band);
    if (ret < 0) {
        wifi_hal_error_print("%s:%d: wifi_getRadioFrequencyBand failed for radio %d\n", __func__,
            __LINE__, radioIndex);
        return RETURN_ERR;
    }
    rcap->band[0] = band;

    memset(pModeStr, 0, HA_STRING_LENGTH_64);
    memset(output_string, 0, HA_STRING_LENGTH_512);

    ret = wifi_getRadioMode(radioIndex, pModeStr, &pMode);
    if (ret < 0) {
        wifi_hal_error_print("%s:%d: wifi_getRadioMode failed for radio %d\n", __func__, __LINE__,
            radioIndex);
        return RETURN_ERR;
    }

    if (wl_map_pureMode_to_operatingStandard(pMode, pModeStr, output_string) < 0) {
        wifi_hal_error_print("%s:%d Failed to map operating standard\n", __func__, __LINE__);
        return RETURN_ERR;
    }

    if (numbands == 1) {
        chlistp = &(rcap->channel_list[0]);
        chlistp->num_channels = channelCount;
        for (j = 0; j < channelCount; j++) {
            chlistp->channels_list[j] = possibleChannels[j];
        }
    }
    for (i = 0; i < numbands; i++) {
        /* channelWidth - all supported bandwidths */
        rcap->channelWidth[i] = 0;
        if (rcap->band[i] & WIFI_FREQUENCY_2_4_BAND) {
            rcap->channelWidth[i] |= (WIFI_CHANNELBANDWIDTH_20MHZ | WIFI_CHANNELBANDWIDTH_40MHZ);
        } else if (rcap->band[i] & (WIFI_FREQUENCY_5_BAND | WIFI_FREQUENCY_6_BAND)) {
            rcap->channelWidth[i] |= (WIFI_CHANNELBANDWIDTH_20MHZ | WIFI_CHANNELBANDWIDTH_40MHZ |
                WIFI_CHANNELBANDWIDTH_80MHZ);
        } else if (rcap->band[i] & (WIFI_FREQUENCY_6_BAND)) {
            rcap->channelWidth[i] |= (WIFI_CHANNELBANDWIDTH_20MHZ | WIFI_CHANNELBANDWIDTH_40MHZ |
                WIFI_CHANNELBANDWIDTH_80MHZ | WIFI_CHANNELBANDWIDTH_160MHZ);
        }

        /* mode - all supported variants */
        rcap->mode[i] = WIFI_80211_VARIANT_H;
        if (strstr(output_string, "b")) {
            rcap->mode[i] |= WIFI_80211_VARIANT_B;
        }
        if (strstr(output_string, "g")) {
            rcap->mode[i] |= WIFI_80211_VARIANT_G;
        }
        if (strstr(output_string, "n")) {
            rcap->mode[i] |= WIFI_80211_VARIANT_N;
        }
        if (strstr(output_string, "a")) {
            rcap->mode[i] |= WIFI_80211_VARIANT_A;
        }
        if (strstr(output_string, "ac")) {
            rcap->mode[i] |= WIFI_80211_VARIANT_AC;
        }
        if (strstr(output_string, "ax")) {
            rcap->mode[i] |= WIFI_80211_VARIANT_AX;
        }

        /* maxBitRate - from mcs_rate_tbl, pick max values for each band
         *      Standard,   BW, GI, NSS,    Rate
         * 2.4G:    {"ax",      "40",   1,  4,  1147},
         * 5G:      {"ax",      "160",  1,  4,  4804},
         * 6G:      {"ax",      "160",  1,  8,  9608},
         */
        rcap->maxBitRate[i] = (rcap->band[i] & WIFI_FREQUENCY_2_4_BAND) ?
            1147 :
            ((rcap->band[i] & WIFI_FREQUENCY_5_BAND) ?
                    4804 :
                    ((rcap->band[i] & (WIFI_FREQUENCY_6_BAND) ? 9608 : 0)));

        /* supportedBitRate - all supported bitrates */
        rcap->supportedBitRate[i] = 0;
        if (rcap->band[i] & WIFI_FREQUENCY_2_4_BAND) {
            rcap->supportedBitRate[i] |= (WIFI_BITRATE_1MBPS | WIFI_BITRATE_2MBPS |
                WIFI_BITRATE_5_5MBPS | WIFI_BITRATE_6MBPS | WIFI_BITRATE_9MBPS |
                WIFI_BITRATE_11MBPS | WIFI_BITRATE_12MBPS);
        } else if (rcap->band[i] & (WIFI_FREQUENCY_5_BAND | WIFI_FREQUENCY_6_BAND)) {
            rcap->supportedBitRate[i] |= (WIFI_BITRATE_6MBPS | WIFI_BITRATE_9MBPS |
                WIFI_BITRATE_12MBPS | WIFI_BITRATE_18MBPS | WIFI_BITRATE_24MBPS |
                WIFI_BITRATE_36MBPS | WIFI_BITRATE_48MBPS | WIFI_BITRATE_54MBPS);
        }

        /* transmitPowerSupported_list - refer wifi_getRadioTransmitPowerSupported */
        ret = wifi_getRadioTransmitPowerSupported(radioIndex, xpwrSupStr);
        if (ret < 0) {
            return RETURN_ERR;
        }
        /* Allow comma or space delimiters in string */
        wl_str2uintArray(xpwrSupStr, ", ", &(rcap->transmitPowerSupported_list[i].numberOfElements),
            rcap->transmitPowerSupported_list[i].transmitPowerSupported,
            MAXNUMBEROFTRANSMIPOWERSUPPORTED);
    } /* for numbands */

    /* autoChannelSupported */
    /* always ON with wifi_getRadioAutoChannelSupported */
    rcap->autoChannelSupported = TRUE;

    /* DCSSupported */
    /* always ON with wifi_getRadioDCSSupported */
    rcap->DCSSupported = TRUE;

    /* zeroDFSSupported - TBD */
    rcap->zeroDFSSupported = FALSE;

    /* csi */
    rcap->csi.maxDevices = CSI_MAX_DEVICES_BRCM;
    rcap->csi.soudingFrameSupported = FALSE; // Setting to false as we dont have the API

    snprintf(rcap->ifaceName, MAXIFACENAMESIZE, "radio%d", radioIndex);

    /* cipher */
    // Harcoding to support all as of now.
    rcap->cipherSupported |= WIFI_CIPHER_CAPA_ENC_CCMP_256 | WIFI_CIPHER_CAPA_ENC_GCMP_256 |
        WIFI_CIPHER_CAPA_ENC_CCMP | WIFI_CIPHER_CAPA_ENC_GCMP | WIFI_CIPHER_CAPA_ENC_TKIP |
        WIFI_CIPHER_CAPA_ENC_BIP | WIFI_CIPHER_CAPA_ENC_BIP_GMAC_128 |
        WIFI_CIPHER_CAPA_ENC_BIP_GMAC_256 | WIFI_CIPHER_CAPA_ENC_BIP_CMAC_256;

    if (wifi_getRadioCountryCode(radioIndex, cbuf) < 0) {
        wifi_hal_error_print("%s:%d wifi_getRadioCountryCode Failed\n", __func__, __LINE__);
    }
    cbuf[2] = '\0';
    for (j = 0; j < (sizeof(countrycode_table) / sizeof(countrycode_table[0])); j++) {
        cctentry = &(countrycode_table[j]);
        if ((strstr(cctentry->str_val, cbuf))) {
            /* match */
            rcap->countrySupported[1] = cctentry->enum_val;
            break;
        }
    } /* for j */
    rcap->numcountrySupported = 1;

    rcap->maxNumberVAPs = MAX_NUM_VAP_PER_RADIO;

    return RETURN_OK;
}

static INT wifi_updateApSecurity(INT apIndex, wifi_vap_security_t *security)
{
    int i = 0;
    char *mfpStr;

    wifi_hal_dbg_print("%s:%d Inside %d %d %s\n", __func__, __LINE__, security->mode,
        security->encr, security->u.key.key);

    if (security->mode == wifi_security_mode_wep_64 ||
        security->mode == wifi_security_mode_wep_128) {
        return RETURN_ERR;
    }

    /* set mfp */
    /* WIFI_HAL_VERSION_3_PHASE2 for RDKM */
    for (i = 0; mfp_table[i].str_val != NULL; ++i) {
        if (mfp_table[i].enum_val == security->mfp)
            break;
    }
    if (mfp_table[i].str_val == NULL) {
        return RETURN_ERR;
    }

    mfpStr = (char *)mfp_table[i].str_val;
    if (wifi_setApSecurityMFPConfig(apIndex, mfpStr)) {
        wifi_hal_error_print("%s:%d wifi_setApSecurityMFPConfig Failed\n", __func__, __LINE__);
        return RETURN_ERR;
    }
    /* set security mode */
    for (i = 0; security_mode_table[i].str_val != NULL; ++i) {
        if (security_mode_table[i].enum_val == security->mode)
            break;
    }
    if (security_mode_table[i].str_val == NULL) {
        return RETURN_ERR;
    }

    if (wifi_setApSecurityModeEnabled(apIndex, (char *)security_mode_table[i].str_val) < 0) {
        wifi_hal_error_print("%s:%d wifi_setApSecurityModeEnabled Failed\n", __func__, __LINE__);
        return RETURN_ERR;
    }
    if (security->mode == wifi_security_mode_none)
        return RETURN_OK;

    /* set wpa encryption */
    for (i = 0; encryption_table[i].str_val != NULL; ++i) {
        if (encryption_table[i].enum_val == security->encr)
            break;
    }

    if (encryption_table[i].str_val == NULL) {
        wifi_hal_error_print("%s:%d Wrong Encryption Failed\n", __func__, __LINE__);
        return RETURN_ERR;
    }

    if (wifi_setApWpaEncryptionMode(apIndex, (char *)encryption_table[i].str_val) < 0) {
        wifi_hal_error_print("%s:%d wifi_setApWpaEncryptionMode Failed\n", __func__, __LINE__);
        return RETURN_ERR;
    }

    /* set wpa psk or passphrase */
    if (security->mode == wifi_security_mode_wpa_personal ||
        security->mode == wifi_security_mode_wpa2_personal ||
        security->mode == wifi_security_mode_wpa3_personal ||
        security->mode == wifi_security_mode_wpa_wpa2_personal ||
        security->mode == wifi_security_mode_wpa3_transition) {
        if (security->u.key.type == wifi_security_key_type_psk) {
            if (wifi_setApSecurityPreSharedKey(apIndex, security->u.key.key) < 0) {
                wifi_hal_error_print("%s:%d wifi_setApSecurityPreSharedKey Failed\n", __func__,
                    __LINE__);
                return RETURN_ERR;
            }

        } else {
            if (wifi_setApSecurityKeyPassphrase(apIndex, security->u.key.key) < 0) {
                wifi_hal_error_print("%s:%d wifi_setApSecurityKeyPassphrase Failed\n", __func__,
                    __LINE__);
                return RETURN_ERR;
            }
        }
        // Wp3 Transition Need to check.
    }

    /* set RADIUS auth server params
     * greylist requires RADIUS params for open mode,
     * but greylist is not configured through this API, and greylist
     * may be enabled after RADIUS params are set, we have to set RADIUS
     * params for open mode even greylist may not be enabled.
     */
    if (security->mode == wifi_security_mode_wpa_enterprise ||
        security->mode == wifi_security_mode_wpa2_enterprise ||
        security->mode == wifi_security_mode_wpa3_enterprise ||
        security->mode == wifi_security_mode_wpa_wpa2_enterprise ||
        security->mode == wifi_security_mode_none) {
        char ip_txt[INET6_ADDRSTRLEN] = { '\0' };
        unsigned int port;
#if defined(WIFI_HAL_VERSION_3_PHASE2)
        int domain;
#endif

#if defined(WIFI_HAL_VERSION_3_PHASE2)
        /* set primary radius auth server ip addr, port, secret */
        if (security->u.radius.ip.family == wifi_ip_family_ipv4) {
            domain = AF_INET;
        } else if (security->u.radius.ip.family == wifi_ip_family_ipv6) {
            domain = AF_INET6;
        } else {
            wifi_hal_error_print("%s:%d unknown IP addr family\n", __func__, __LINE__);
            return RETURN_ERR;
        }
        /* addr of IPv4adddr and IPv6addr is the same as they are in the same union */
        if (inet_ntop(domain, (void *)security->u.radius.ip.u.IPv6addr, ip_txt, sizeof(ip_txt)) ==
            NULL) {
            wifi_hal_error_print("%s:%d fail to convert primary RADIUS server ip addr \n", __func__,
                __LINE__);
            return RETURN_ERR;
        }
#else /* WIFI_HAL_VERSION_3_PHASE2 */
        snprintf(ip_txt, sizeof(ip_txt), (char *)security->u.radius.ip);
#endif /* WIFI_HAL_VERSION_3_PHASE2 */

        if (wifi_setApSecurityRadiusServer(apIndex, ip_txt, security->u.radius.port,
                security->u.radius.key) < 0) {
            wifi_hal_error_print("%s:%d wifi_setApSecurityRadiusServer Failed \n", __func__,
                __LINE__);
            return RETURN_ERR;
        }

#if defined(WIFI_HAL_VERSION_3_PHASE2)
        /* set secondary radius auth server ip addr, port, secret */
        if (security->u.radius.s_ip.family == wifi_ip_family_ipv4) {
            if (security->u.radius.s_ip.u.IPv4addr == 0) {
                /* So far, we cannot know explicitly how secondary RADIUS auth server
                 * is not set , but mostly likely, memory for secondary RADIUS auth
                 * server is set to all '\0'
                 *
                 * TODO: To discuss how to decide secondary RADIUS auth server
                 * is not set in a better way */
                return RETURN_OK;
            }
            domain = AF_INET;
        } else if (security->u.radius.s_ip.family == wifi_ip_family_ipv6) {
            domain = AF_INET6;
        } else {
            /* consider this as secondary RADIUS auth is not configured,
             * until we have a better way to decide secondary RADIUS is not set */
            return RETURN_OK;
        }
        /* addr of IPv4adddr and IPv6addr is the same as they are in the same union */
        if (inet_ntop(domain, (void *)security->u.radius.s_ip.u.IPv6addr, ip_txt, sizeof(ip_txt)) ==
            NULL) {
            /* Don't consider this as error */
            return RETURN_OK;
        }
#else /* WIFI_HAL_VERSION_3_PHASE2 */
        snprintf(ip_txt, sizeof(ip_txt), (char *)security->u.radius.s_ip);
#endif /* WIFI_HAL_VERSION_3_PHASE2 */
        if (wifi_setApSecuritySecondaryRadiusServer(apIndex, ip_txt, security->u.radius.s_port,
                security->u.radius.s_key) < 0) {
            wifi_hal_error_print("%s:%d wifi_setApSecurityRadiusServer Failed \n", __func__,
                __LINE__);
        }

        return RETURN_OK;
    }

    return RETURN_OK;
}

static BOOL is_wifi_hal_vap_private(UINT ap_index)
{
    unsigned int index = 0;
    for (index = 0; index < get_sizeof_interfaces_index_map(); index++) {
        if ((interface_index_map[index].index == ap_index) &&
            (strncmp(interface_index_map[index].vap_name, "private_ssid", strlen("private_ssid")) ==
                0)) {
            return true;
        }
    }
    return false;
}

static BOOL is_wifi_hal_vap_xhs(UINT ap_index)
{
    unsigned int index = 0;
    for (index = 0; index < get_sizeof_interfaces_index_map(); index++) {
        if ((interface_index_map[index].index == ap_index) &&
            (strncmp(interface_index_map[index].vap_name, "iot_ssid", strlen("iot_ssid")) == 0)) {
            return true;
        }
    }
    return false;
}

INT wifi_getApSecurity(INT apIndex, wifi_vap_security_t *security)
{
    wifi_hal_dbg_print("%s:%d Inside \n", __func__, __LINE__);
    int ret, i;
    char security_mode[128] = { '\0' }, encryption[128] = { '\0' }, mfpStr[32] = { '\0' };

    memset(security, 0, sizeof(*security));

    /* get security mode */
    if (wifi_getApSecurityModeEnabled(apIndex, security_mode) < 0) {
        wifi_hal_error_print("%s:%d wifi_getApSecurityModeEnabled Failed\n", __func__, __LINE__);
        return RETURN_ERR;
    }

    for (i = 0; security_mode_table[i].str_val != NULL; ++i) {
        if (!strcmp(security_mode_table[i].str_val, security_mode)) {
            security->mode = security_mode_table[i].enum_val;
            break;
        }
    }
    if (security_mode_table[i].str_val == NULL) {
        wifi_hal_error_print("%s:%d Unsupported Security mode\n", __func__, __LINE__);
        return RETURN_ERR;
    }
    /* get mfp */
    if (wifi_getApSecurityMFPConfig(apIndex, mfpStr) < 0) {
        wifi_hal_error_print("%s:%d wifi_getApSecurityMFPConfig Failed\n", __func__, __LINE__);
        return RETURN_ERR;
    }
    for (i = 0; mfp_table[i].str_val != NULL; ++i) {
        if (!strcmp(mfp_table[i].str_val, mfpStr)) {
            security->mfp = mfp_table[i].enum_val;
            break;
        }
    }
    if (mfp_table[i].str_val == NULL) {
        wifi_hal_error_print("%s:%d Unsupported MFP Value\n", __func__, __LINE__);
        return RETURN_ERR;
    }

    if (security->mode == wifi_security_mode_none)
        return RETURN_OK;

    /* get wep key */
    if (security->mode == wifi_security_mode_wep_64 ||
        security->mode == wifi_security_mode_wep_128) {
        return RETURN_ERR; /* wep not supported */
    }
    /* get wpa encryption */
    if (wifi_getApWpaEncryptionMode(apIndex, encryption) < 0) {
        wifi_hal_error_print("%s:%d wifi_getApWpaEncryptionMode Failed\n", __func__, __LINE__);
        return RETURN_ERR;
    }

    for (i = 0; encryption_table[i].str_val != NULL; ++i) {
        if (!strcmp(encryption_table[i].str_val, encryption)) {
            security->encr = encryption_table[i].enum_val;
            break;
        }
    }

    if (encryption_table[i].str_val == NULL) {
        wifi_hal_error_print("%s:%d Unsupported Encryption\n", __func__, __LINE__);
        return RETURN_ERR;
    }

    /* get wpa psk or passphrase */
    if (security->mode == wifi_security_mode_wpa_personal ||
        security->mode == wifi_security_mode_wpa2_personal ||
        security->mode == wifi_security_mode_wpa3_personal ||
        security->mode == wifi_security_mode_wpa_wpa2_personal ||
        security->mode == wifi_security_mode_wpa3_transition) {
        if (wifi_getApSecurityKeyPassphrase(apIndex, security->u.key.key) == 0) {
            if (security->mode == wifi_security_mode_wpa3_personal)
                security->u.key.type = wifi_security_key_type_sae;
            else if (security->mode == wifi_security_mode_wpa3_transition)
                security->u.key.type = wifi_security_key_type_psk_sae;
            else
                security->u.key.type = wifi_security_key_type_pass;
        } else { /* not passphrase, try to get psk */
            if (wifi_getApSecurityPreSharedKey(apIndex, security->u.key.key) == 0) {
                security->u.key.type = wifi_security_key_type_psk;
            } else {
                wifi_hal_error_print("%s:%d Failed ot get PSK or Passphrase\n", __func__, __LINE__);
                return RETURN_ERR;
            }
        }

        /* get wpa3_transition_disable */
    }
    /* get RADIUS auth server params */
    if (security->mode == wifi_security_mode_wpa_enterprise ||
        security->mode == wifi_security_mode_wpa2_enterprise ||
        security->mode == wifi_security_mode_wpa3_enterprise ||
        security->mode == wifi_security_mode_wpa_wpa2_enterprise) {
        char ip_txt[INET6_ADDRSTRLEN] = { '\0' };
        unsigned int port;

        /* get primary radius auth server ip addr, port, secret */
        if (wifi_getApSecurityRadiusServer(apIndex, ip_txt, &port, security->u.radius.key) < 0) {
            wifi_hal_error_print("%s:%d Failed ot get PSK or Passphrase\n", __func__, __LINE__);
            return RETURN_ERR;
        }
#if defined(WIFI_HAL_VERSION_3_PHASE2)
        if (inet_pton(AF_INET, ip_txt, &security->u.radius.ip.u.IPv4addr) > 0) {
            security->u.radius.ip.family = wifi_ip_family_ipv4;
        } else if (inet_pton(AF_INET6, ip_txt, security->u.radius.ip.u.IPv6addr) > 0) {
            security->u.radius.ip.family = wifi_ip_family_ipv6;
        } else {
            return RETURN_ERR;
        }
#else /* WIFI_HAL_VERSION_3_PHASE2 */
        snprintf(security->u.radius.ip, sizeof(security->u.radius.ip), ip_txt);
#endif /* WIFI_HAL_VERSION_3_PHASE2 */
        security->u.radius.port = (unsigned short)port;

        /* get secondary radius auth server ip addr, port, secret,
         * it is not an error if secondary radius server is not set */
        if (wifi_getApSecuritySecondaryRadiusServer(apIndex, ip_txt, &port,
                security->u.radius.s_key) == 0) {
#if defined(WIFI_HAL_VERSION_3_PHASE2)
            if (inet_pton(AF_INET, ip_txt, &security->u.radius.s_ip.u.IPv4addr) > 0) {
                security->u.radius.s_ip.family = wifi_ip_family_ipv4;
            } else if (inet_pton(AF_INET6, ip_txt, security->u.radius.s_ip.u.IPv6addr) > 0) {
                security->u.radius.s_ip.family = wifi_ip_family_ipv6;
            } else {
                return RETURN_ERR;
            }
#else /* WIFI_HAL_VERSION_3_PHASE2 */
            snprintf(security->u.radius.s_ip, sizeof(security->u.radius.s_ip), ip_txt);
#endif /* WIFI_HAL_VERSION_3_PHASE2 */

            security->u.radius.s_port = (unsigned short)port;
        }
    }

    return RETURN_OK;
}

INT wifi_setApWpsConfiguration(INT apIndex, wifi_wps_t *wpsConfig)
{
    wifi_hal_dbg_print("%s:%d Inside \n", __func__, __LINE__);

    /* Set WPS enable */
    if (wifi_setApWpsEnable(apIndex, wpsConfig->enable) < 0) {
        wifi_hal_error_print("%s:%d wifi_setApWpsEnable Failed \n", __func__, __LINE__);
        return RETURN_ERR;
    }

    if (!wpsConfig->enable)
        return RETURN_OK;

    /* Set WPS device PIN */
    if (wpsConfig->pin[0] != '\0') {
        if (wifi_setApWpsDevicePIN(apIndex, wpsConfig->pin[0]) < 0) {
            wifi_hal_error_print("%s:%d wifi_setApSecurityRadiusServer Failed \n", __func__,
                __LINE__);
            return RETURN_ERR;
        }
    }

    /* Set enabled WPS config methods */
    if (wpsConfig->methods) {
        char enabled_methods[512] = { '\0' }; // buffer is big enough to hold all methods
        int i = 0, size = 0, len = 0;

        len = sizeof(enabled_methods);
        for (i = 0; wps_config_method_table[i].str_val != NULL; ++i) {
            if (wpsConfig->methods & wps_config_method_table[i].enum_val) {
                if (enabled_methods[0] == '\0')
                    size = snprintf(enabled_methods, len, "%s", wps_config_method_table[i].str_val);
                else
                    size += snprintf(enabled_methods + size, len - size, ",%s",
                        wps_config_method_table[i].str_val);
            }
        }
        if (wifi_setApWpsConfigMethodsEnabled(apIndex, enabled_methods) < 0) {
            wifi_hal_error_print("%s:%d wifi_setApWpsConfigMethodsEnabled Failed \n", __func__,
                __LINE__);
            return RETURN_ERR;
        }
    }
    return RETURN_OK;
}

static int ovs_add_br(const char *brname)
{
    wifi_hal_dbg_print("%s:%d ovs-vsctl add-br %s\n", __func__, __LINE__, brname);
    char command[100] = { 0 };
    snprintf(command, sizeof(command), "/usr/bin/ovs-vsctl --may-exist add-br %s\n", brname);
    wifi_hal_dbg_print("%s:%d Command is %s\n", __func__, __LINE__, command);

    system(command);
    return 0;
}

static int ovs_br_exists(char *brname)
{
    char buf[128] = {};
    char *p;
    FILE *f;

    f = popen("/usr/bin/ovs-vsctl list-br", "r");
    while (f && (p = fgets(buf, sizeof(buf), f))) {
        if (!strcmp(strsep(&p, "\n") ?: "", brname)) {
            if (f)
                pclose(f);
            return 0;
        }
    }

    if (f)
        pclose(f);
    return -1;
}

static int ovs_br_add_if(char *brname, char *ifname)
{
    wifi_hal_dbg_print("%s:%d ovs-vsctl add-port %s %s\n", __func__, __LINE__, brname, ifname);
    char command[100] = { 0 };
    snprintf(command, sizeof(command), "/usr/bin/ovs-vsctl --may-exist add-port %s %s\n", brname,
        ifname);
    wifi_hal_dbg_print("%s:%d Command is %s\n", __func__, __LINE__, command);

    system(command);
    return 0;
}

static void create_bridge(char *vap_name, char *bridge_name)
{
    wifi_hal_dbg_print("%s:%d Inside \n", __func__, __LINE__);
    if (ovs_br_exists(bridge_name) == 0) {
        if (ovs_br_add_if(bridge_name, vap_name) != 0) {
            wifi_hal_error_print("%s:%d adding interface:%s to bridge:%s failed\n", __func__,
                __LINE__, vap_name, bridge_name);
            return;
        }
    } else {
        if (ovs_add_br(bridge_name) == 0) {
            if (ovs_br_add_if(bridge_name, vap_name) != 0) {
                wifi_hal_error_print("%s:%d adding interface:%s to bridge:%s failed\n", __func__,
                    __LINE__, vap_name, bridge_name);
                return;
            }
        }
    }
    return;
}

INT wifi_hal_createVAP(wifi_radio_index_t index, wifi_vap_info_map_t *map)
{
    wifi_hal_dbg_print("%s:%d Inside radio %d\n", __func__, __LINE__, index);
    int i, apIndex, ret, enable, result = RETURN_OK;
    char encMode[HA_STRING_LENGTH_64] = { 0 };
    int no_apply = 0;
    int wps_no_apply = 0;
    int security_no_apply = 0;
    char name[SSID_STRING_LEN] = { 0 };
    unsigned char UAPSDEnable = 0;
    unsigned char WMEnable = 0;
    int local_nbr_val = 0;
    BOOL curr_vap_state = false;
    wifi_wps_t event;
    wifi_vap_security_t *tmp_security;

    wifi_vap_info_map_t *tmp_map = (wifi_vap_info_map_t *)malloc(sizeof(wifi_vap_info_map_t));
    memset(tmp_map, 0, sizeof(wifi_vap_info_map_t));
    memcpy(tmp_map, map, sizeof(wifi_vap_info_map_t));

    tmp_security = (wifi_vap_security_t *)malloc(sizeof(wifi_vap_security_t));
    memset(tmp_security, 0, sizeof(wifi_vap_security_t));
    for (i = 0; i < map->num_vaps; i++) {

        apIndex = map->vap_array[i].vap_index;

        if (!map->vap_array[i].u.bss_info.enabled) {
            wifi_hal_info_print("%s:%d ap_index:%d not enabled\n", __func__, __LINE__, apIndex);

            curr_vap_state = false;
            wifi_getApEnable(apIndex, &curr_vap_state);
            if (curr_vap_state == true) {
                ret = wifi_setApEnable(apIndex, FALSE);
                if (ret < 0) {
                    wifi_hal_error_print("%s:%d wifi_setApEnable Failed\n", __func__, __LINE__);
                    goto free;
                }
            }

            continue;
        }

        ret = wifi_setApEnable(apIndex, TRUE);

        if (ret < 0) {
            wifi_hal_error_print("%s:%d wifi_setApEnable Failed\n", __func__, __LINE__);
            goto free;
        }

        snprintf(name, SSID_STRING_LEN, "%s", map->vap_array[i].u.bss_info.ssid);

        if (wifi_setSSIDName(apIndex, name) < 0) {
            wifi_hal_error_print("%s:%d wifi_setSSIDName Failed\n", __func__, __LINE__);
            goto free;
        }

        if (wifi_setApSsidAdvertisementEnable(apIndex, map->vap_array[i].u.bss_info.showSsid) < 0) {
            wifi_hal_error_print("%s:%d wifi_setSSIDName Failed\n", __func__, __LINE__);
            goto free;
        }

        if (wifi_setApIsolationEnable(apIndex, map->vap_array[i].u.bss_info.isolation) < 0) {
            wifi_hal_error_print("%s:%d  wifi_setApIsolationEnable Failed\n", __func__, __LINE__);
            goto free;
        }

        if (wifi_setApManagementFramePowerControl(apIndex,
                map->vap_array[i].u.bss_info.mgmtPowerControl) < 0) {
            wifi_hal_error_print("%s:%d   wifi_setApManagementFramePowerControl Failed\n", __func__,
                __LINE__);
            goto free;
        }

        if (map->vap_array[i].u.bss_info.bssMaxSta > 0) {
            if (wifi_setApMaxAssociatedDevices(apIndex, map->vap_array[i].u.bss_info.bssMaxSta) <
                0) {
                wifi_hal_error_print("%s:%d  wifi_setApMaxAssociatedDevices Failed\n", __func__,
                    __LINE__);
                goto free;
            }
        }

        if (wifi_setBSSTransitionActivation(apIndex,
                map->vap_array[i].u.bss_info.bssTransitionActivated) < 0) {
            wifi_hal_error_print("%s:%d  wifi_setBSSTransitionActivation Failed\n", __func__,
                __LINE__);
            goto free;
        }

        bool rrm_val = false;
        if (wifi_getNeighborReportActivation(apIndex, (unsigned char *)&rrm_val) < 0) {
            wifi_hal_error_print("%s:%d  wifi_getNeighborReportActivation Failed\n", __func__,
                __LINE__);
            goto free;
        }

        if (wifi_setNeighborReportActivation(apIndex, rrm_val) < 0) {
            wifi_hal_error_print("%s:%d wifi_setNeighborReportActivation  Failed\n", __func__,
                __LINE__);
            goto free;
        }

        memset(tmp_security, 0, sizeof(wifi_vap_security_t));
        memcpy(tmp_security, &(map->vap_array[i].u.bss_info.security), sizeof(wifi_vap_security_t));

        if (wifi_updateApSecurity(apIndex, &(map->vap_array[i].u.bss_info.security)) < 0) {
            wifi_hal_error_print("%s:%d  wifi_updateApSecurity Failed\n", __func__, __LINE__);
            goto free;
        }

        if (wifi_pushApInterworkingElement(apIndex,
                &(map->vap_array[i].u.bss_info.interworking.interworking)) < 0) {
            wifi_hal_error_print("%s:%d wifi_pushApInterworkingElement  Failed\n", __func__,
                __LINE__);
            goto free;
        }

        int enable = 0;
        if (map->vap_array[i].u.bss_info.mac_filter_enable) {
            if (map->vap_array[i].u.bss_info.mac_filter_mode == wifi_mac_filter_mode_black_list) {
                enable = 2;
            } else {
                enable = 1;
            }
        } else {
            enable = 0;
        }

        if (wifi_setApMacAddressControlMode(apIndex, enable) < 0) {
            wifi_hal_error_print("%s:%d wifi_pushApInterworkingElement  Failed\n", __func__,
                __LINE__);
            goto free;
        }

        if (wifi_setApWmmEnable(apIndex, map->vap_array[i].u.bss_info.wmm_enabled) < 0) {
            wifi_hal_error_print("%s:%d wifi_setApWmmEnable Failed \n", __func__, __LINE__);
            goto free;
        }

        if (wifi_setApWpsConfiguration(apIndex, &(map->vap_array[i].u.bss_info.wps)) < 0) {
            wifi_hal_error_print("%s:%d wifi_setApWpsConfiguration Failed \n", __func__, __LINE__);
            goto free;
        }
        char interface_name[8] = { 0 };
        get_wifi_interface_name_from_vap_index(map->vap_array[i].vap_index, interface_name);
        create_bridge(interface_name, map->vap_array[i].bridge_name);
    }
    wifi_hal_dbg_print("%s:%d Outside \n", __func__, __LINE__);

    wifi_apply();
    free(tmp_security);
    free(tmp_map);
    tmp_security = NULL;
    tmp_map = NULL;

    return RETURN_OK;

free:
    free(tmp_security);
    free(tmp_map);
    tmp_security = NULL;
    tmp_map = NULL;
    return RETURN_ERR;
}

INT wifi_getApWpsConfiguration(INT apIndex, wifi_wps_t *wpsConfig)
{
    char enabled_methods[512], *method, *rest;
    int i;

    wifi_hal_dbg_print("%s:%d Inside \n", __func__, __LINE__);

    memset(wpsConfig, 0, sizeof(*wpsConfig));

    if (wifi_getApWpsEnable(apIndex, &(wpsConfig->enable)) < 0) {
        wifi_hal_error_print("%s:%d wifi_getApWpsEnable Failed\n", __func__, __LINE__);
        return RETURN_ERR;
    }

    if (!wpsConfig->enable) {
        return RETURN_ERR;
    }

    /* Get WPS device PIN */
    unsigned long pin_num = strtoul(wpsConfig->pin, NULL, 10);
    if (wifi_getApWpsDevicePIN(apIndex, &pin_num) < 0) {
        wifi_hal_error_print("%s:%d wifi_getApWpsDevicePIN Failed\n", __func__, __LINE__);
        return RETURN_ERR;
    }

    /* Get enabled WPS config methods */
    if (wifi_getApWpsConfigMethodsEnabled(apIndex, enabled_methods) < 0) {
        wifi_hal_error_print("%s:%d  wifi_getApWpsConfigMethodsEnabled Failed\n", __func__,
            __LINE__);
        return RETURN_OK;
    }

    method = strtok_r(enabled_methods, ",", &rest);
    while (method != NULL) {
        i = 0;
        while (TRUE) {
            if (wps_config_method_table[i].str_val == NULL) {
                break;
            }

            if (!strcmp(method, wps_config_method_table[i].str_val)) {
                wpsConfig->methods |= wps_config_method_table[i].enum_val;
                break;
            }
            ++i;
        }
        method = strtok_r(NULL, ",", &rest);
    }

    return RETURN_OK;
}

#define MACF_TO_MAC(macstr, mac)                                                           \
    sscanf(macstr, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx", &mac[0], &mac[1], &mac[2], \
        &mac[3], &mac[4], &mac[5])

INT wifi_hal_getRadioVapInfoMap(wifi_radio_index_t index, wifi_vap_info_map_t *map)
{
    wifi_hal_info_print("%s:%d Inside \n", __func__, __LINE__);
    int apIndex, ret, len, enable, result = RETURN_OK;
    char bssid[32] = { 0 };
    unsigned int i = 0, j = 0;
    unsigned int vap_num = 0;

    while (strlen(interface_index_map[j].vap_name) != 0) {
        if (interface_index_map[j].rdk_radio_index != index) {
            j++;
            continue;
        }

        map->vap_array[i].vap_index = interface_index_map[j].index;

        strncpy(map->vap_array[i].vap_name, interface_index_map[j].vap_name,
            sizeof(interface_index_map[j].vap_name) - 1);
        map->vap_array[i].radio_index = interface_index_map[j].rdk_radio_index;

        apIndex = interface_index_map[j].index;

        if (wifi_getSSIDName(apIndex, map->vap_array[i].u.bss_info.ssid) < 0) {
            wifi_hal_error_print("%s:%d wifi_getSSIDName Failed\n", __func__, __LINE__);
            result = RETURN_ERR;
        }
        if (wifi_getApEnable(apIndex, &(map->vap_array[i].u.bss_info.enabled)) < 0) {
            wifi_hal_error_print("%s:%d wifi_getApEnable Failed\n", __func__, __LINE__);
            result = RETURN_ERR;
        }

        if (wifi_getApSsidAdvertisementEnable(apIndex, &(map->vap_array[i].u.bss_info.showSsid)) <
            0) {
            wifi_hal_error_print("%s:%d wifi_getApSsidAdvertisementEnable Failed\n", __func__,
                __LINE__);
            result = RETURN_ERR;
        }

        if (wifi_getApManagementFramePowerControl(apIndex,
                &(map->vap_array[i].u.bss_info.mgmtPowerControl)) < 0) {
            wifi_hal_error_print("%s:% d wifi_getApManagementFramePowerControl Failed\n", __func__,
                __LINE__);
            result = RETURN_ERR;
        }

        if (wifi_getApMaxAssociatedDevices(apIndex, &(map->vap_array[i].u.bss_info.bssMaxSta)) <
            0) {
            wifi_hal_error_print("%s:%d wifi_getApMaxAssociatedDevices Failed\n", __func__,
                __LINE__);
            result = RETURN_ERR;
        }

        if (wifi_getBSSTransitionActivation(apIndex,
                &(map->vap_array[i].u.bss_info.bssTransitionActivated)) < 0) {
            wifi_hal_error_print("%s:%d wifi_getBSSTransitionActivation Failed\n", __func__,
                __LINE__);
            result = RETURN_ERR;
        }

        if (wifi_getNeighborReportActivation(apIndex,
                &(map->vap_array[i].u.bss_info.nbrReportActivated)) < 0) {
            wifi_hal_error_print("%s:%d  wifi_getNeighborReportActivation Failed\n", __func__,
                __LINE__);
            result = RETURN_ERR;
        }

        ret = wifi_getApSecurity(apIndex, &(map->vap_array[i].u.bss_info.security));
        if (ret != RETURN_OK) {
            result = RETURN_ERR;
        }

        if (wifi_getApInterworkingElement(apIndex,
                &(map->vap_array[i].u.bss_info.interworking.interworking)) < 0) {
            wifi_hal_error_print("%s:%d  wifi_getApInterworkingElement Failed\n", __func__,
                __LINE__);
            result = RETURN_ERR;
        }

        if (wifi_getSSIDMACAddress(apIndex, bssid) < 0) {
            wifi_hal_error_print("%s:%d wifi_getSSIDMACAddress  Failed\n", __func__, __LINE__);
            result = RETURN_ERR;
        }
        if (strlen(bssid) < 17) {
            wifi_hal_error_print("Invalid BSSID string: %s\n", bssid);
            result = RETURN_ERR;
        } else {
            MACF_TO_MAC(bssid, map->vap_array[i].u.bss_info.bssid);
            wifi_hal_dbg_print("%s:%d: Mac address : %02X:%02X:%02X:%02X:%02X:%02X\n", __func__,
                __LINE__, map->vap_array[i].u.bss_info.bssid[0],
                map->vap_array[i].u.bss_info.bssid[1], map->vap_array[i].u.bss_info.bssid[2],
                map->vap_array[i].u.bss_info.bssid[3], map->vap_array[i].u.bss_info.bssid[4],
                map->vap_array[i].u.bss_info.bssid[5]);
        }
        vap_num++;
        i++;
        j++;
    }
    map->num_vaps = vap_num;
    return result;
}

INT wifi_hal_getHalCapability(wifi_hal_capability_t *cap)
{
    int i, len, ret, bsdSupport = 0;
    memset(cap, 0, sizeof(wifi_hal_capability_t));
    wifi_interface_name_idex_map_t *interface_map = NULL;
    radio_interface_mapping_t *radio_interface_map = NULL;

    interface_map = malloc(interface_index_map_size * sizeof(wifi_interface_name_idex_map_t));
    if (interface_map == NULL) {
        wifi_hal_error_print("Memory is not allocated");
        return RETURN_ERR;
    }

    radio_interface_map = malloc(l_radio_interface_map_size * sizeof(radio_interface_mapping_t));
    if (radio_interface_map == NULL) {
        wifi_hal_error_print("Memory is not allocated");
        free(interface_map);
        interface_map = NULL;
        return RETURN_ERR;
    }
    /* version */
    cap->version.major = WIFI_HAL_MAJOR_VERSION;
    cap->version.minor = WIFI_HAL_MINOR_VERSION;

    cap->wifi_prop.numRadios = g_wifi_hal.num_radios;
    /* get RadioCapabilities */
    for (i = 0; i < cap->wifi_prop.numRadios; i++) {
        ret = wl_getRadioCapabilities(i, &(cap->wifi_prop.radiocap[i]));
        if (ret < 0) {
            free(interface_map);
            free(radio_interface_map);
            interface_map = NULL;
            radio_interface_map = NULL;
            wifi_hal_error_print("%s:%d  wl_getRadioCapabilities Failed\n", __func__, __LINE__);
            return RETURN_ERR;
        }
    }
    cap->wifi_prop.colocated_mode = -1;

    get_wifi_interface_info_map(interface_map);

    for (i = 0; i < interface_index_map_size; i++) {
        if (strlen(interface_map[i].vap_name) == 0) {
            continue;
        }
        cap->wifi_prop.interface_map[i].phy_index = interface_map[i].phy_index;
        cap->wifi_prop.interface_map[i].rdk_radio_index = interface_map[i].rdk_radio_index;
        strncpy(cap->wifi_prop.interface_map[i].interface_name, interface_map[i].interface_name,
            sizeof(cap->wifi_prop.interface_map[i].interface_name) - 1);
        strncpy(cap->wifi_prop.interface_map[i].bridge_name, interface_map[i].bridge_name,
            sizeof(cap->wifi_prop.interface_map[i].bridge_name) - 1);
        cap->wifi_prop.interface_map[i].vlan_id = interface_map[i].vlan_id;
        cap->wifi_prop.interface_map[i].index = interface_map[i].index;
        strncpy(cap->wifi_prop.interface_map[i].vap_name, interface_map[i].vap_name,
            sizeof(interface_map[i].vap_name) - 1);
    }

    get_radio_interface_info_map(radio_interface_map);

    for (i = 0; i < l_radio_interface_map_size; i++) {
        cap->wifi_prop.radio_interface_map[i].phy_index = radio_interface_map[i].phy_index;
        cap->wifi_prop.radio_interface_map[i].radio_index = radio_interface_map[i].radio_index;
        cap->wifi_prop.radio_presence[i] = TRUE;
        strncpy(cap->wifi_prop.radio_interface_map[i].radio_name, radio_interface_map[i].radio_name,
            sizeof(cap->wifi_prop.radio_interface_map[i].radio_name) - 1);
        strncpy(cap->wifi_prop.radio_interface_map[i].interface_name,
            radio_interface_map[i].interface_name,
            sizeof(cap->wifi_prop.radio_interface_map[i].interface_name) - 1);
    }
    wifi_hal_dbg_print("%s %d\n", __func__, __LINE__);
    if (wifi_getBandSteeringCapability(&(cap->BandSteeringSupported)) < 0) {
        wifi_hal_error_print("%s:%d  wifi_getBandSteeringCapability Failed\n", __func__, __LINE__);
    }
    wifi_hal_dbg_print("%s %d\n", __func__, __LINE__);
    free(interface_map);
    free(radio_interface_map);
    return RETURN_OK;
}

static wifi_radio_operationParam_t *get_radio_oper(int index)
{
    if (index < 0 || index >= MAX_NUM_RADIOS) {
        return NULL;
    }
    return &g_radio_oper_param[index];
}

INT wifi_hal_setRadioOperatingParameters(wifi_radio_index_t index,
    wifi_radio_operationParam_t *operationParam)
{
    int len, tblSize, i, ret = 0;
    unsigned int local_beacon_period;
    char channelMode[HA_STRING_LENGTH_32] = "11", variant[HA_STRING_LENGTH_32];
    char operStd[HA_STRING_LENGTH_32] = { 0 };
    char ch_str[HA_STRING_LENGTH_16] = { 0 };
    char ch_list[HA_STRING_LENGTH_512] = { 0 };
    char temp_bw[HA_STRING_LENGTH_16] = { 0 };
    unsigned int pureMode = PMODE_NONE, tempVariant = 0, channelWidth = 0, local_channel = 0;
    bool is_channel_changed;
    BOOL local_auto_channel;
    BOOL enable;
    BOOL output_bool;
    wifi_radio_operationParam_t *old_param;

    len = sizeof(enable);
    // RadioEnable
    wifi_getRadioEnable(index, &enable);

    if (enable != operationParam->enable) {
        if (wifi_setRadioEnable(index, operationParam->enable) < 0) {
            wifi_hal_error_print("%s:%d  wifi_setRadioEnable Failed\n", __func__, __LINE__);
            return RETURN_ERR;
        }
    }

    tempVariant = operationParam->variant & ~(WIFI_80211_VARIANT_H);

    wifi_hal_dbg_print("%s: radioIndex = %d, Enable=%d operationParam->variant=%d\n", __FUNCTION__,
        index, enable, operationParam->variant);

    /* Translate Variant to oper std mode and append to channel mode */
    tblSize = ARRAY_SIZE(std2ieee80211Variant_infoTable);
    for (i = 0; i < tblSize; i++) {
        if (tempVariant & std2ieee80211Variant_infoTable[i].enum_val) {
            strncpy(variant, std2ieee80211Variant_infoTable[i].str_val, sizeof(variant));
            variant[sizeof(variant) - 1] = '\0';
            str_to_upper(variant);
            strcat(channelMode, variant);
            break;
        }
    }

    /* Translate bandwidth and append to channel mode */
    tblSize = ARRAY_SIZE(wifi_bandwidth_infoTable);
    for (i = 0; i < tblSize; i++) {
        if (operationParam->channelWidth == wifi_bandwidth_infoTable[i].enum_val) {
            if ((index == 1) && (strcmp(wifi_bandwidth_infoTable[i].str_val, "160") == 0)) {
                wifi_getRadioDfsEnable(index, &output_bool);
                if (output_bool == FALSE) {
                    /*Cannot use 160MHz BW when DFS is disabled, keep the current BW*/
                    if (wifi_getRadioOperatingChannelBandwidth(index, temp_bw) != RETURN_OK) {
                        wifi_hal_error_print(
                            "%s: wifi_getRadioOperatingChannelBandwidth returns err!\n",
                            __FUNCTION__);
                    } else {
                        /* Store the enum for the current bw in operationParam->channelWidth */
                        operationParam->channelWidth = get_bandwidth_enum_from_string(temp_bw);
                        /* Loop again */
                        i = 0;
                        continue;
                    }
                }
            }
            strncat(channelMode, wifi_bandwidth_infoTable[i].str_val,
                sizeof(channelMode) - strlen(channelMode) - 1);
            channelWidth = atoi(wifi_bandwidth_infoTable[i].str_val);
            break;
        }
    }

    wifi_hal_dbg_print("%s: radioIndex = %d, operationParam channelWidth=%d variant=%d\n",
        __FUNCTION__, index, operationParam->channelWidth, operationParam->variant);

    /* Translate Variant to oper std mode and append to channel mode */
    tblSize = ARRAY_SIZE(std2ieee80211Variant_infoTable);
    for (i = tblSize - 1; i >= 0; i--) {
        if (tempVariant & std2ieee80211Variant_infoTable[i].enum_val) {
            strncat(operStd, std2ieee80211Variant_infoTable[i].str_val,
                sizeof(operStd) - strlen(operStd) - 1);
            strncat(operStd, ",", sizeof(operStd) - strlen(operStd) - 1);
        }
    }

    len = strlen(operStd);
    operStd[len - 1] = '\0';

    for (i = 0; operStdPMode_infoTable[i].operStd != NULL; i++) {
        if (!(strncmp(operStdPMode_infoTable[i].operStd, operStd,
                strlen(operStdPMode_infoTable[i].operStd)))) {
            /* match */
            pureMode = operStdPMode_infoTable[i].pmodeVal;
            break;
        }
    }

    wifi_hal_info_print("%s: radioIndex = %d, channelMode = %s pureMode=%d operStd %s\n",
        __FUNCTION__, index, channelMode, pureMode, operStd);

    old_param = (wifi_radio_operationParam_t *)malloc(sizeof(wifi_radio_operationParam_t));

    memcpy(old_param, get_radio_oper(index), sizeof(wifi_radio_operationParam_t));

    is_channel_changed = old_param->channel != operationParam->channel ||
        old_param->channelWidth != operationParam->channelWidth;

    if (is_channel_changed) {
        old_param->channel = operationParam->channel;
        old_param->channelWidth = operationParam->channelWidth;
    }

    if (is_channel_changed &&
        memcmp((unsigned char *)old_param, (unsigned char *)operationParam,
            sizeof(wifi_radio_operationParam_t)) == 0) {
        if (wifi_pushRadioChannel2(index, operationParam->channel, channelWidth,
                DEFAULT_CSA_BEACON_COUNT) < 0) {
            wifi_hal_error_print("%s:%d wifi_pushRadioChannel2 \n", __func__, __LINE__);
            goto free;
        }
        free(old_param);
        old_param = NULL;
        return RETURN_OK;
    } else {

        if (wifi_setRadioMode(index, channelMode, pureMode) < 0) {
            wifi_hal_error_print("%s: wl_setRadioMode failed\n", __FUNCTION__);
        }

        if (wifi_setRadioChannel(index, operationParam->channel) < 0) {
            wifi_hal_error_print("%s:%d wifi_setRadioChannel \n", __func__, __LINE__);
            goto free;
        }

        /* Set the beacon Interval */
        wifi_getRadioBeaconPeriod(index, &local_beacon_period);
        if (local_beacon_period != operationParam->beaconInterval) {
            if (operationParam->beaconInterval) {
                if (wifi_setRadioBeaconPeriod(index, operationParam->beaconInterval) < 0) {
                    wifi_hal_error_print("%s:%d wifi_setRadioBeaconPeriod \n", __func__, __LINE__);
                    goto free;
                }
            }
        }

        /* Check If the Band is 5G and if DFS is enabled */
        wifi_getRadioDfsEnable(index, &output_bool);
        if ((operationParam->band == WIFI_FREQUENCY_5_BAND) &&
            (output_bool != operationParam->DfsEnabled)) {
            if (wifi_setRadioDfsEnable(index, operationParam->DfsEnabled) != RETURN_OK) {
                wifi_hal_error_print("%s:%d RadioDfsEnable Failed\n", __func__, __LINE__);
                goto free;
            }
        }

        len = sizeof(local_auto_channel);
        wifi_getRadioAutoChannelEnable(index, &local_auto_channel);
        if (local_auto_channel != operationParam->autoChannelEnabled) {
            /* enable/disable auto channel mode */
            if (wifi_setRadioAutoChannelEnable(index, operationParam->autoChannelEnabled) < 0) {
                wifi_hal_error_print("%s:%d  wifi_setRadioAutoChannelEnable Failed\n", __func__,
                    __LINE__);
            }
        }
    }

    memcpy(get_radio_oper(index), operationParam, sizeof(wifi_radio_operationParam_t));
    free(old_param);
    old_param = NULL;
    return RETURN_OK;

free:
    free(old_param);
    old_param = NULL;
    return RETURN_ERR;
}

INT wifi_hal_setApWpsPin(INT ap_index, char *wps_pin)
{
    wifi_setApWpsEnrolleePin(ap_index, wps_pin);
    return RETURN_OK;
}

INT wifi_hal_kickAssociatedDevice(INT apIndex, mac_address_t mac_addr)
{
    wifi_hal_dbg_print("%s:%d Inside \n", __func__, __LINE__);

    char bmac[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    if (memcmp(mac_addr, bmac, sizeof(bmac))) {
        // not Handling Kick all as of now.
        return RETURN_OK;
    }

    mac_addr_str_t mac_str;

    to_mac_str(mac_addr, mac_str);
    if (wifi_kickApAssociatedDevice(apIndex, mac_str) < 0) {
        wifi_hal_error_print("%s:%d wifi_kickApAssociatedDevice Failed\n", __func__, __LINE__);
    }
    return RETURN_OK;
}

INT wifi_hal_startNeighborScan(INT apIndex, wifi_neighborScanMode_t scan_mode, INT dwell_time,
    UINT chan_num, UINT *chan_list)
{
    return wifi_startNeighborScan(apIndex, scan_mode, dwell_time, chan_num, chan_list);
}

INT wifi_hal_getNeighboringWiFiStatus(INT radioIndex, wifi_neighbor_ap2_t **neighbor_ap_array,
    UINT *output_array_size)
{
    return wifi_getNeighboringWiFiStatus(radioIndex, neighbor_ap_array, output_array_size);
}

INT wifi_hal_wifi_send_mgmt_frame_responseget_default_ssid(char *ssid, int vap_index)
{
    return RETURN_OK;
}

INT wifi_hal_get_default_country_code(char *code)
{
    int i, vap_index = 0;
    if (!code) {
        wifi_hal_error_print("%s:%d: NULL pointer for country code\n", __func__, __LINE__);
        return -1;
    }

    for (i = 0; i < g_ifaceCount; i++) {
        if (iface_def_val_map[i].vap_index == vap_index) {
            snprintf(code, HA_STRING_LENGTH_64, "%s", iface_def_val_map[i].default_country_code);
            wifi_hal_dbg_print("%s:%d Found vap_index %d, default country code: %s\n", __func__,
                __LINE__, vap_index, iface_def_val_map[i].default_country_code);
            return 0;
        }
    }
    wifi_hal_error_print("%s:%d: vap_index: %d not found!\n", __func__, __LINE__, vap_index);
    return -1;
}

INT wifi_hal_configNeighborReports(UINT apIndex, bool enable, bool auto_resp)
{
    return RETURN_OK;
}

INT wifi_hal_set_acs_keep_out_chans(wifi_radio_operationParam_t *wifi_radio_oper_param,
    int radioIndex)
{
    return RETURN_OK;
}

INT wifi_hal_send_mgmt_frame_response(int ap_index, int type, int status, int status_code,
    uint8_t *frame, uint8_t *mac, int len, int rssi)
{
    return RETURN_OK;
}

bool is_db_upgrade_required(char *inactive_firmware)
{
    return false;
}

INT wifi_hal_startScan(wifi_radio_index_t index, wifi_neighborScanMode_t scan_mode, INT dwell_time,
    UINT num, UINT *chan_list)
{
    return RETURN_OK;
}

void wifi_hal_ap_max_client_rejection_callback_register(wifi_apMaxClientRejection_callback func)
{
    return;
}

void wifi_hal_radius_eap_failure_callback_register(wifi_radiusEapFailure_callback func)
{
    return;
}

INT wifi_hal_disconnect(INT ap_index)
{
    return RETURN_OK;
}

void wifi_hal_disassoc(int vap_index, int status, uint8_t *mac)
{
    return;
}

INT wifi_hal_connect(INT ap_index, wifi_bss_info_t *bss)
{
    return RETURN_OK;
}

void wifi_hal_register_frame_hook(wifi_hal_frame_hook_fn_t func)
{
    return;
}

INT wifi_vapstatus_callback_register(wifi_vapstatus_callback func)
{
    return RETURN_OK;
}

INT wifi_hal_getRadioTemperature(wifi_radio_index_t radioIndex,
    wifi_radioTemperature_t *radioPhyTemperature)
{
    // Going to affect Levl Funtionality.
    return RETURN_OK;
}

INT wifi_hal_pre_init()
{
    int i = 0;
    init_interface_map();
    /* update the global structure with interface mapping */
    g_wifi_hal.num_radios = l_radio_interface_map_size;
    for (i = 0; i < g_wifi_hal.num_radios; i++) {
        g_wifi_hal.radio_info[i].index = l_radio_interface_map[i].phy_index;
        g_wifi_hal.radio_info[i].rdk_radio_index = l_radio_interface_map[i].radio_index;
        g_wifi_hal.radio_info[i].capab.maxNumberVAPs = MAX_NUM_VAP_PER_RADIO;
        strncpy(g_wifi_hal.radio_info[i].name, l_radio_interface_map[i].radio_name,
            sizeof(g_wifi_hal.radio_info[i].name));
        g_wifi_hal.radio_info[i].capab.index = l_radio_interface_map[i].radio_index;
        strncpy(g_wifi_hal.radio_info[i].capab.ifaceName, l_radio_interface_map[i].interface_name,
            sizeof(g_wifi_hal.radio_info[i].capab.ifaceName) - 1);
    }

    return RETURN_OK;
}

INT wifi_anqpSendResponse(UINT apIndex, mac_address_t sta, unsigned char token,
    wifi_anqp_node_t *head)
{
    return RETURN_OK;
}

void wifi_hal_staConnectionStatus_callback_register(wifi_staConnectionStatus_callback func)
{
    return;
}

INT wifi_setGASConfiguration(UINT advertisementID, wifi_GASConfiguration_t *input_struct)
{
    return RETURN_OK;
}

INT wifi_sendActionFrameExt(INT apIndex, mac_address_t MacAddr, UINT frequency, UINT wait,
    UCHAR *frame, UINT len)
{
    return RETURN_OK;
}

int enablePassPointSettings(int ap_index, BOOL passpoint_enable, BOOL downstream_disable,
    BOOL p2p_disable, BOOL layer2TIF)
{
    return 0;
}

INT wifi_anqp_request_callback_register(wifi_anqp_request_callback_t anqpReqCallback)
{
    return RETURN_OK;
}

INT wifi_hal_setApWpsCancel(INT ap_index)
{
    return RETURN_OK;
}

void wifi_hal_scanResults_callback_register(wifi_scanResults_callback func)
{
    return;
}

INT wifi_hal_getScanResults(wifi_radio_index_t index, wifi_channel_t *channel,
    wifi_bss_info_t **bss, UINT *num_bss)
{
    return RETURN_OK;
}

void wifi_hal_set_neighbor_report(uint apIndex, uint add, mac_address_t mac)
{
    wifi_hal_info_print("%s:%d Enter %d\n", __func__, __LINE__, apIndex);
    wifi_NeighborReport_t nbr_report;
    memcpy(nbr_report.bssid, mac, sizeof(mac_address_t));
    wifi_setNeighborReports(apIndex, add, &nbr_report);

    return;
}

INT wifi_hal_mgmt_frame_callbacks_register(wifi_receivedMgmtFrame_callback func)
{
    return RETURN_OK;
}

INT wifi_hal_sendDataFrame(int vap_id, unsigned char *dmac, unsigned char *data_buff, int data_len,
    BOOL insert_llc, int protocol, int priority)
{
    return RETURN_OK;
}

static wifi_channelBandwidth_t get_bandwidth_enum_from_string(char *bw_str)
{
    if (bw_str == NULL) {
        return WIFI_CHANNELBANDWIDTH_20MHZ;
    }

    if (strstr(bw_str, "320")) {
        return WIFI_CHANNELBANDWIDTH_320MHZ;
    } else if (strstr(bw_str, "160")) {
        return WIFI_CHANNELBANDWIDTH_160MHZ;
    } else if (strstr(bw_str, "80")) {
        if (strstr(bw_str, "80+80")) {
            return WIFI_CHANNELBANDWIDTH_80_80MHZ;
        }
        return WIFI_CHANNELBANDWIDTH_80MHZ;
    } else if (strstr(bw_str, "40")) {
        return WIFI_CHANNELBANDWIDTH_40MHZ;
    } else if (strstr(bw_str, "20")) {
        return WIFI_CHANNELBANDWIDTH_20MHZ;
    }

    return WIFI_CHANNELBANDWIDTH_20MHZ; // Default
}

static const wifi_chan_block_t wifi_5ghz_map[] = {
    // ---- 20 MHz ----
    { 36,  20,  { 36 },                                     1 },
    { 40,  20,  { 40 },                                     1 },
    { 44,  20,  { 44 },                                     1 },
    { 48,  20,  { 48 },                                     1 },
    { 52,  20,  { 52 },                                     1 },
    { 56,  20,  { 56 },                                     1 },
    { 60,  20,  { 60 },                                     1 },
    { 64,  20,  { 64 },                                     1 },
    { 100, 20,  { 100 },                                    1 },
    { 104, 20,  { 104 },                                    1 },
    { 108, 20,  { 108 },                                    1 },
    { 112, 20,  { 112 },                                    1 },
    { 116, 20,  { 116 },                                    1 },
    { 120, 20,  { 120 },                                    1 },
    { 124, 20,  { 124 },                                    1 },
    { 128, 20,  { 128 },                                    1 },
    { 132, 20,  { 132 },                                    1 },
    { 136, 20,  { 136 },                                    1 },
    { 140, 20,  { 140 },                                    1 },
    { 144, 20,  { 144 },                                    1 },
    { 149, 20,  { 149 },                                    1 },
    { 153, 20,  { 153 },                                    1 },
    { 157, 20,  { 157 },                                    1 },
    { 161, 20,  { 161 },                                    1 },

    // ---- 40 MHz ----
    { 38,  40,  { 36, 40 },                                 2 },
    { 46,  40,  { 44, 48 },                                 2 },
    { 54,  40,  { 52, 56 },                                 2 },
    { 62,  40,  { 60, 64 },                                 2 },
    { 102, 40,  { 100, 104 },                               2 },
    { 110, 40,  { 108, 112 },                               2 },
    { 118, 40,  { 116, 120 },                               2 },
    { 126, 40,  { 124, 128 },                               2 },
    { 134, 40,  { 132, 136 },                               2 },
    { 151, 40,  { 149, 153 },                               2 },
    { 159, 40,  { 157, 161 },                               2 },

    // ---- 80 MHz ----
    { 42,  80,  { 36, 40, 44, 48 },                         4 },
    { 58,  80,  { 52, 56, 60, 64 },                         4 },
    { 106, 80,  { 100, 104, 108, 112 },                     4 },
    { 122, 80,  { 116, 120, 124, 128 },                     4 },
    { 138, 80,  { 132, 136, 140, 144 },                     4 },
    { 155, 80,  { 149, 153, 157, 161 },                     4 },

    // ---- 160 MHz ----
    { 50,  160, { 36, 40, 44, 48, 52, 56, 60, 64 },         8 },
    { 114, 160, { 100, 104, 108, 112, 116, 120, 124, 128 }, 8 },
};

const wifi_chan_block_t *find_chan_from_center_channel(ULONG center)
{
    size_t total = sizeof(wifi_5ghz_map) / sizeof(wifi_5ghz_map[0]);
    for (size_t i = 0; i < total; i++) {
        if (wifi_5ghz_map[i].center == center)
            return &wifi_5ghz_map[i];
    }
    return NULL;
}

static ULONG last_dfs_channel = 0;

static void chan_event_cb(UINT radioIndex, wifi_chan_eventType_t event,
    wifi_radar_eventType_t radar_type, ULONG channel, ULONG dfs_channel)
{
    wifi_hal_dbg_print("Received event, radioIndex = %d ", radioIndex);
    char output_string[HA_STRING_LENGTH_64] = { 0 };
    INT ret = 0;

    if (g_callbacks.channel_change_event_callback == NULL) {
        wifi_hal_dbg_print("No registered OneWiFi event handler!\n");
        return;
    }

    pthread_mutex_lock(&g_chan_event_mutex);
    wifi_channel_change_event_t wifi_channel_change_event;
    memset(&wifi_channel_change_event, 0, sizeof(wifi_channel_change_event));

    wifi_channel_change_event.radioIndex = radioIndex;
    wifi_channel_change_event.channel = channel;
    ret = wifi_getRadioOperatingChannelBandwidth(radioIndex, output_string);
    if (ret != RETURN_OK) {
        wifi_hal_error_print("Failed to get channel bandwidth\n");
    }

    wifi_channel_change_event.channelWidth = get_bandwidth_enum_from_string(output_string);

    if (dfs_channel != 0) {
        last_dfs_channel = dfs_channel;
    }

    switch (event) {
    case WIFI_EVENT_CHANNELS_CHANGED:
        wifi_channel_change_event.channel = channel;
        wifi_channel_change_event.event = WIFI_EVENT_CHANNELS_CHANGED;
        wifi_channel_change_event.sub_event = radar_type;
        wifi_hal_dbg_print("CHANNELS CHANGED, last_channel = %d\n", channel);
        break;

    case WIFI_EVENT_DFS_RADAR_DETECTED:
        if (wifi_channel_change_event.sub_event == WIFI_EVENT_RADAR_DETECTED) {
            // Need to find the proper channel from the centre channel
            const wifi_chan_block_t *channel_block = find_chan_from_center_channel(channel);
            if (channel_block != NULL) {
                for (int i = 0; i < channel_block->count; i++) {
                    if (last_dfs_channel == channel_block->members[i]) {
                        wifi_channel_change_event.channel = channel_block->members[i];
                    }
                }
            }
        } else {
            wifi_channel_change_event.channel = channel;
        }
        wifi_hal_dbg_print("DFS RADAR DETECTED, last_channel = %d\n",
            wifi_channel_change_event.channel);
        wifi_channel_change_event.event = WIFI_EVENT_DFS_RADAR_DETECTED;
        wifi_channel_change_event.sub_event = radar_type;
        break;

    default:
        wifi_hal_dbg_print("Unknown event\n");
        return;
    }

    g_callbacks.channel_change_event_callback(wifi_channel_change_event);
    pthread_mutex_unlock(&g_chan_event_mutex);
}

static void chan_event_dfs_radar_cb(UINT radioIndex, wifi_chan_eventType_t event, UCHAR channel)
{
    wifi_hal_dbg_print("Received event, radioIndex = %d ", radioIndex);
    char output_string[HA_STRING_LENGTH_64] = { 0 };
    INT ret = 0;

    if (g_callbacks.channel_change_event_callback == NULL) {
        wifi_hal_dbg_print("No registered OneWiFi event handler!\n");
        return;
    }

    wifi_channel_change_event_t wifi_channel_change_event;
    memset(&wifi_channel_change_event, 0, sizeof(wifi_channel_change_event));

    wifi_channel_change_event.radioIndex = radioIndex;
    wifi_channel_change_event.channel = channel;
    wifi_channel_change_event.event = event;
    ret = wifi_getRadioOperatingChannelBandwidth(radioIndex, output_string);
    if (ret != RETURN_OK) {
        wifi_hal_error_print("Failed to get channel bandwidth\n");
    }

    wifi_channel_change_event.channelWidth = get_bandwidth_enum_from_string(output_string);

    switch (event) {
    case WIFI_EVENT_DFS_RADAR_DETECTED:
        wifi_hal_dbg_print("DFS RADAR DETECTED, last_channel = %d\n", channel);
        wifi_channel_change_event.sub_event = WIFI_EVENT_RADAR_DETECTED;
        chan_event_cb(radioIndex, WIFI_EVENT_DFS_RADAR_DETECTED, WIFI_EVENT_RADAR_DETECTED, channel,
            last_dfs_channel);
        break;

    default:
        wifi_hal_dbg_print("Unknown event\n");
        return;
    }
}

INT wifi_chan_event_register(wifi_chan_event_CB_t event_cb)
{
    wifi_hal_dbg_print("%s:%d Registered for channel change\n", __func__, __LINE__);
    g_callbacks.channel_change_event_callback = event_cb;

    return RETURN_OK;
}

// platform Specific APIs, used by OneWifi.
int platform_get_channel_bandwidth(wifi_radio_index_t index, wifi_channelBandwidth_t *channelWidth)
{
    // called in OneWIfi migration scenario to update wifi db from PSM.
    return 0;
}

int nvram_get_current_ssid(char *l_ssid, int vap_index)
{
    int i = 0;

    if (!l_ssid) {
        wifi_hal_error_print("%s:%d Invalid ssid pointer\n", __func__, __LINE__);
        return -1;
    }

    for (i = 0; i < g_ifaceCount; i++) {
        if (iface_def_val_map[i].vap_index == vap_index) {
            snprintf(l_ssid, HA_STRING_LENGTH_64, "%s", iface_def_val_map[i].default_ssid);
            wifi_hal_dbg_print("%s:%d Found vap_index %d, SSID: %s\n", __func__, __LINE__,
                vap_index, iface_def_val_map[i].default_ssid);
            return 0;
        }
    }

    wifi_hal_error_print("%s:%d vap_index %d not found\n", __func__, __LINE__, vap_index);
    return -1;
}

int nvram_get_vap_enable_status(BOOL *vap_enable, int vap_index)
{

    if (!vap_enable) {
        wifi_hal_error_print("%s:%d: Invalid argument (NULL)\n", __func__, __LINE__);
        return -1;
    }

    for (int i = 0; i < g_ifaceCount; i++) {
        if (iface_def_val_map[i].vap_index == vap_index) {
            BOOL enable = iface_def_val_map[i].vap_enable;
            *vap_enable = enable ? TRUE : FALSE;

            wifi_hal_dbg_print("%s:%d: vap enable status:%d for vap index:%d\r\n", __func__,
                __LINE__, *vap_enable, vap_index);
            return 0;
        }
    }

    wifi_hal_error_print("%s:%d: No matching interface found for vap index %d\r\n", __func__,
        __LINE__, vap_index);
    return -1;
}

static int nvram_get_actual_password(char *l_password, int vap_index)
{
    int i = 0;

    if (!l_password) {
        wifi_hal_error_print("%s:%d Invalid ssid pointer\n", __func__, __LINE__);
        return -1;
    }

    for (i = 0; i < g_ifaceCount; i++) {
        if (iface_def_val_map[i].vap_index == vap_index) {
            snprintf(l_password, HA_STRING_LENGTH_64, "%s",
                iface_def_val_map[i].default_passphrase);
            wifi_hal_dbg_print("%s:%d Found vap_index %d, SSID: %s\n", __func__, __LINE__,
                vap_index, iface_def_val_map[i].default_passphrase);
            return 0;
        }
    }

    wifi_hal_error_print("%s:%d vap_index %d not found\n", __func__, __LINE__, vap_index);
    return -1;
}

int nvram_get_radio_enable_status(BOOL *radio_enable, int vap_index)
{
    if (!radio_enable) {
        wifi_hal_error_print("%s:%d: Invalid argument (NULL)\n", __func__, __LINE__);
        return -1;
    }

    for (int i = 0; i < g_ifaceCount; i++) {
        if (iface_def_val_map[i].vap_index == vap_index) {
            BOOL enable = iface_def_val_map[i].radio_enable;
            *radio_enable = enable ? TRUE : FALSE;

            wifi_hal_dbg_print("%s:%d: vap enable status:%d for vap index:%d\r\n", __func__,
                __LINE__, *radio_enable, vap_index);
            return 0;
        }
    }

    wifi_hal_error_print("%s:%d: No matching interface found for vap index %d\r\n", __func__,
        __LINE__, vap_index);
    return -1;
}

int nvram_get_current_password(char *l_password, int vap_index)
{
    return nvram_get_actual_password(l_password, vap_index);
}

int nvram_get_mgmt_frame_power_control(int vap_index, int *output_dbm)
{
    if (!output_dbm) {
        wifi_hal_error_print("%s:%d: Invalid output_dbm pointer\n", __func__, __LINE__);
        return -1;
    }

    for (int i = 0; i < g_ifaceCount; i++) {
        if (iface_def_val_map[i].vap_index == vap_index) {
            *output_dbm = iface_def_val_map[i].mgmt_frame_pwr_ctrl;

            wifi_hal_dbg_print("%s:%d: vap_index:%d -> mgmt_frame_pwr_ctrl:%d dBm\n", __func__,
                __LINE__, vap_index, *output_dbm);

            return 0; // Success
        }
    }

    wifi_hal_error_print("%s:%d: vap_index:%d not found\n", __func__, __LINE__, vap_index);
    return -1;
}

int nvram_get_current_security_mode(wifi_security_modes_t *security_mode, int vap_index)
{
    char sec_mode_str[HA_STRING_LENGTH_64] = { 0 };
    char mfp_str[HA_STRING_LENGTH_64] = { 0 };
    wifi_security_modes_t current_security_mode;
    bool found = false;

    if (!security_mode) {
        wifi_hal_error_print("%s:%d: NULL pointer for security_mode\n", __func__, __LINE__);
        return -1;
    }

    for (int i = 0; i < g_ifaceCount; i++) {
        if (iface_def_val_map[i].vap_index == vap_index) {
            snprintf(sec_mode_str, sizeof(sec_mode_str), "%s",
                iface_def_val_map[i].secure_mode_akm);
            snprintf(mfp_str, sizeof(mfp_str), "%s", iface_def_val_map[i].secure_mode_mfp);

            wifi_hal_dbg_print("%s:%d Found vap_index %d, Akm: %s, Mfp: %s\n", __func__, __LINE__,
                vap_index, sec_mode_str, mfp_str);
            found = true;
            break;
        }
    }

    if (!found) {
        wifi_hal_error_print("%s:%d: vap_index %d not found in iface_def_val_map\n", __func__,
            __LINE__, vap_index);
        return -1;
    }

    if (get_security_mode_int_from_str(sec_mode_str, mfp_str, &current_security_mode) == 0) {
        *security_mode = current_security_mode;
        return 0;
    }

    wifi_hal_error_print("%s:%d: Failed to convert sec_mode_str=%s mfp_str=%s\n", __func__,
        __LINE__, sec_mode_str, mfp_str);
    return -1;
}

INT wifi_hal_get_default_keypassphrase(char *password, int vap_index)
{
    wifi_hal_dbg_print("%s:%d vap_index: %d\r\n", __func__, __LINE__, vap_index);

    if (is_wifi_hal_vap_private(vap_index)) {
        if (!password) {
            wifi_hal_error_print("%s:%d: NULL pointer for password\n", __func__, __LINE__);
            return -1;
        }
        for (int i = 0; i < g_ifaceCount; i++) {
            if (iface_def_val_map[i].vap_index == vap_index) {
                snprintf(password, HA_STRING_LENGTH_64, "%s",
                    iface_def_val_map[i].default_wifi_pwd);
                wifi_hal_dbg_print("%s:%d Found vap_index %d, password: %s\n", __func__, __LINE__,
                    vap_index, iface_def_val_map[i].default_wifi_pwd);
                return 0;
            }
        }
    } else if (is_wifi_hal_vap_xhs(vap_index)) {
        wifi_hal_dbg_print("%s:%d vap_index: %d\r\n", __func__, __LINE__, vap_index);
        return nvram_get_actual_password(password, vap_index);
        wifi_hal_dbg_print("%s:%d vap_index: %d pw: %s \r\n", __func__, __LINE__, vap_index,
            password);
    } else {
        wifi_hal_dbg_print("%s:%d vap_index: %d\r\n", __func__, __LINE__, vap_index);
        return nvram_get_actual_password(password, vap_index);
        wifi_hal_dbg_print("%s:%d vap_index: %d pw: %s \r\n", __func__, __LINE__, vap_index,
            password);
    }
    return -1;
}

INT wifi_hal_get_default_ssid(char *ssid, int vap_index)
{

    wifi_hal_dbg_print("%s:%d vap_index: %d\r\n", __func__, __LINE__, vap_index);

    if (is_wifi_hal_vap_private(vap_index)) {
        if (!ssid) {
            wifi_hal_error_print("%s:%d: NULL pointer for ssid\n", __func__, __LINE__);
            return -1;
        }
        for (int i = 0; i < g_ifaceCount; i++) {
            if (iface_def_val_map[i].vap_index == vap_index) {
                snprintf(ssid, HA_STRING_LENGTH_64, "%s",
                    iface_def_val_map[i].default_factory_ssid);
                wifi_hal_dbg_print("%s:%d Found vap_index %d, SSID: %s\n", __func__, __LINE__,
                    vap_index, iface_def_val_map[i].default_factory_ssid);
                return 0;
            }
        }
        wifi_hal_error_print("%s:%d: vap_index: %d not found!\n", __func__, __LINE__, vap_index);

    } else if (is_wifi_hal_vap_xhs(vap_index)) {
        return nvram_get_current_ssid(ssid, vap_index);
        wifi_hal_dbg_print("%s:%d ssid: %s\r\n", __func__, __LINE__, ssid);
    } else {
        return nvram_get_current_ssid(ssid, vap_index);
        wifi_hal_dbg_print("%s:%d ssid: %s\r\n", __func__, __LINE__, ssid);
    }
    return -1;
}

INT wifi_hal_get_default_radius_key(char *radius_key, int vap_index)
{
    if (!radius_key) {
        wifi_hal_error_print("%s:%d: NULL pointer for radius_key\n", __func__, __LINE__);
        return -1;
    }
    for (int i = 0; i < g_ifaceCount; i++) {
        if (iface_def_val_map[i].vap_index == vap_index) {
            snprintf(radius_key, HA_STRING_LENGTH_64, "%s",
                iface_def_val_map[i].default_radius_key);
            wifi_hal_dbg_print("%s:%d Found vap_index %d, radius_key\n: %s\n", __func__, __LINE__,
                vap_index, iface_def_val_map[i].default_radius_key);
            return 0;
        }
    }
    wifi_hal_error_print("%s:%d: vap_index: %d not found!\n", __func__, __LINE__, vap_index);
    return -1;
}

INT wifi_hal_get_default_wps_pin(char *wps_pin)
{
    int i, vap_index = 0;
    if (!wps_pin) {
        wifi_hal_error_print("%s:%d: NULL pointer for wps_pin\n", __func__, __LINE__);
        return -1;
    }
    for (i = 0; i < g_ifaceCount; i++) {
        if (iface_def_val_map[i].vap_index == vap_index) {
            snprintf(wps_pin, HA_STRING_LENGTH_64, "%s", iface_def_val_map[i].default_wps_pin);
            wifi_hal_dbg_print("%s:%d Found vap_index %d, wps_pin: %s\n", __func__, __LINE__,
                vap_index, iface_def_val_map[i].default_wps_pin);
            return 0;
        }
    }
    wifi_hal_error_print("%s:%d: vap_index: %d not found!\n", __func__, __LINE__, vap_index);
    return -1;
}

static const unsigned int dfs_channels_5ghz[] = { 52, 56, 60, 64, /* U-NII-2 */
    100, 104, 108, 112, /* U-NII-2e */
    116, 120, 124, 128, 132, 136, 140, 144 };

static BOOL channel_in_list(unsigned int channel, const unsigned int *list, size_t list_size)
{
    for (size_t i = 0; i < list_size; ++i) {
        if (list[i] == channel)
            return TRUE;
    }
    return FALSE;
}

static BOOL is_channel_dfs(unsigned int channel, wifi_freq_bands_t band)
{
    if (channel == 0)
        return FALSE;

    if (!(band & (WIFI_FREQUENCY_5_BAND | WIFI_FREQUENCY_5L_BAND | WIFI_FREQUENCY_5H_BAND)))
        return FALSE;

    return channel_in_list(channel, dfs_channels_5ghz, ARRAY_SIZE(dfs_channels_5ghz));
}

static void *wifi_hal_monitor_thread(void *arg)
{
    wifi_hal_dbg_print("%s:%d: Channel monitor thread started %d\n", __func__, __LINE__,
        g_wifi_hal.num_radios);

    g_monitor_thread_running = true;
    ULONG channel = 0;
    wifi_channelMap_t last_channel[MAX_NUM_RADIOS] = { 0 };
    wifi_freq_bands_t band = 0;
    BOOL is_dfs = FALSE;
    INT ret = RETURN_ERR;
    INT idx = 0;
    wifi_radar_eventType_t radar_type = CHAN_STATE_DFS_NOP_FINISHED;
    wifi_channelMap_t radio_channels[HA_STRING_LENGTH_128], *chan;

    while (g_monitor_thread_running) {
        for (int i = 0; ((i < g_wifi_hal.num_radios) && (i < MAX_NUM_RADIOS)); i++) {
            if (wifi_getRadioChannel(i, &channel) != RETURN_OK) {
                wifi_hal_error_print("%s:%d: Failed to get radio %d channel\n", __func__, __LINE__,
                    i);
                continue;
            }

            if (wifi_getRadioFrequencyBand(i, &band) != RETURN_OK) {
                wifi_hal_error_print("%s:%d: Failed to get radio %d band\n", __func__, __LINE__, i);
                continue;
            }

            if (wifi_getRadioDfsEnable(i, &is_dfs) != RETURN_OK) {
                wifi_hal_error_print("%s:%d: Failed to get radio %d DFS status\n", __func__,
                    __LINE__, i);
                continue;
            }

            // Checking if the channel is DFS channel and DFS is enabled on the radio.
            // If it's a DFS channel we will be sending events to OneWifi only on channel state
            // changes.
            if (is_channel_dfs(channel, band) && is_dfs) {
                if (last_channel[i].ch_number != channel) {
                    chan_event_cb(i, WIFI_EVENT_CHANNELS_CHANGED, 0, channel, last_dfs_channel);
                }
                memset(&radio_channels, 0, sizeof(radio_channels));
                ret = wifi_getRadioChannels(i, radio_channels, ARRAY_SIZE(radio_channels));
                if (ret == RETURN_OK) {
                    for (idx = 0; idx < (int)ARRAY_SIZE(radio_channels); idx++) {
                        chan = &radio_channels[idx];

                        if (!is_channel_dfs(chan->ch_number, band))
                            continue;

                        if (chan->ch_number != channel)
                            continue;

                        if ((last_channel[i].ch_number == chan->ch_number) &&
                            (last_channel[i].ch_state == chan->ch_state)) {
                            break;
                        }
                        wifi_hal_dbg_print("%s:%d: Radar type %d on channel %d last channel %d, "
                                           "last chan state %d\n",
                            __func__, __LINE__, chan->ch_state, chan->ch_number,
                            last_channel[i].ch_number, last_channel[i].ch_state);

                        memset(&last_channel[i], 0, sizeof(wifi_channelMap_t));
                        memcpy(&last_channel[i], chan, sizeof(wifi_channelMap_t));

                        switch (chan->ch_state) {

                        case CHAN_STATE_DFS_NOP_FINISHED:
                        case CHAN_STATE_AVAILABLE:
                            // wifi_getRadioChannels seems to toggle NOP_FINISHED state and
                            // STATE_AVAILABLE on a radar PULSE detection/Channel change to DFS
                            // channel for split seconds. So ignoring this state, As it's not
                            // properly handled in OneWifi.
                            continue;
                            break;

                        case CHAN_STATE_DFS_CAC_START:
                            radar_type = WIFI_EVENT_RADAR_CAC_STARTED;
                            break;

                        case CHAN_STATE_DFS_CAC_COMPLETED:
                            radar_type = WIFI_EVENT_RADAR_CAC_FINISHED;
                            break;

                        case CHAN_STATE_DFS_NOP_START:
                            // OneWifi is not handling NOP_START event. So ignoring this state.
                            continue;
                            break;

                        default:
                            continue;
                            break;
                        }

                        chan_event_cb(i, WIFI_EVENT_DFS_RADAR_DETECTED, radar_type, chan->ch_number,
                            last_channel[i].ch_number);
                    }
                }
            } else {
                if (last_channel[i].ch_number != channel) {
                    last_channel[i].ch_number = channel;
                    chan_event_cb(i, WIFI_EVENT_CHANNELS_CHANGED, 0, channel, last_dfs_channel);
                }
            }
        }
        sleep(3);
    }
    wifi_hal_dbg_print("%s:%d: Channel monitor thread exiting\n", __func__, __LINE__);
    return NULL;
}

INT wifi_hal_post_init(wifi_vap_info_map_t *vap_map)
{
    int thread_ret = pthread_create(&g_monitor_thread, NULL, wifi_hal_monitor_thread, NULL);
    if (thread_ret != 0) {
        wifi_hal_error_print("%s:%d Failed to create monitor thread: %d\n", __func__, __LINE__,
            thread_ret);
    }
    if (wifi_chan_eventRegister(chan_event_dfs_radar_cb) != RETURN_OK) {
        wifi_hal_dbg_print("Failed to register chan event callback\n");
    }
    return RETURN_OK;
}

INT wifi_hal_init()
{
    unsigned int ret = RETURN_OK;
    wifi_hal_dbg_print("%d %s\n", __LINE__, __func__);

    ret = wifi_init();
    wifi_hal_dbg_print("%d %s ret: %d\n", __LINE__, __func__, ret);
    return RETURN_OK;
}

INT wifi_hal_setApMacAddressControlMode(INT apIndex, INT filterMode)
{
    int ret = RETURN_OK;

    ret = wifi_setApMacAddressControlMode(apIndex, filterMode);
    wifi_hal_dbg_print("%d %s ret: %d\n", __LINE__, __func__, ret);
    if (ret == RETURN_OK) {
        return RETURN_OK;
    }
    return RETURN_ERR;
}

INT wifi_hal_delApAclDevice(INT apIndex, CHAR *DeviceMacAddress)
{
    int ret = RETURN_OK;

    ret = wifi_delApAclDevice(apIndex, DeviceMacAddress);
    wifi_hal_dbg_print("%d %s ret: %d\n", __LINE__, __func__, ret);
    if (ret == RETURN_OK) {
        return RETURN_OK;
    }
    return RETURN_ERR;
}

INT wifi_hal_addApAclDevice(INT apIndex, CHAR *DeviceMacAddress)
{
    int ret = RETURN_OK;

    ret = wifi_addApAclDevice(apIndex, DeviceMacAddress);
    wifi_hal_dbg_print("%d %s ret: %d\n", __LINE__, __func__, ret);

    if (ret == RETURN_OK) {
        return RETURN_OK;
    }
    return RETURN_ERR;
}

INT wifi_hal_setApWpsButtonPush(INT apIndex)
{
    int ret = RETURN_OK;

    ret = wifi_setApWpsButtonPush(apIndex);
    wifi_hal_dbg_print("%d %s ret: %d\n", __LINE__, __func__, ret);

    if (ret == RETURN_OK) {
        return RETURN_OK;
    }
    return RETURN_ERR;
}

INT wifi_hal_delApAclDevices(INT apIndex)
{
    int ret = RETURN_OK;

    ret = wifi_delApAclDevices(apIndex);
    wifi_hal_dbg_print("%d %s ret: %d\n", __LINE__, __func__, ret);

    if (ret == RETURN_OK) {
        return RETURN_OK;
    }
    return RETURN_ERR;
}

INT wifi_hal_setBTMRequest(UINT apIndex, CHAR *peerMac, wifi_BTMRequest_t *request)
{
    int ret = RETURN_OK;

    ret = wifi_setBTMRequest(apIndex, peerMac, request);
    wifi_hal_dbg_print("%d %s ret: %d\n", __LINE__, __func__, ret);

    if (ret == RETURN_OK) {
        return RETURN_OK;
    }
    return RETURN_ERR;
}

INT wifi_hal_setRMBeaconRequest(UINT apIndex, CHAR *peer_mac, wifi_BeaconRequest_t *request,
    UCHAR *out_DialogToken)
{
    int ret = RETURN_OK;

    ret = wifi_setRMBeaconRequest(apIndex, peer_mac, request, out_DialogToken);
    wifi_hal_dbg_print("%d %s ret: %d\n", __LINE__, __func__, ret);

    if (ret == RETURN_OK) {
        return RETURN_OK;
    }
    return RETURN_ERR;
}

INT wifi_hal_setNeighborReports(UINT apIndex, UINT numNeighborReports,
    wifi_NeighborReport_t *neighborReports)
{
    int ret = RETURN_OK;

    ret = wifi_setNeighborReports(apIndex, numNeighborReports, neighborReports);
    wifi_hal_dbg_print("%d %s ret: %d\n", __LINE__, __func__, ret);

    if (ret == RETURN_OK) {
        return RETURN_OK;
    }
    return RETURN_ERR;
}

INT wifi_apDeAuthEvent_cb_func(INT apIndex, char *MAC, INT reason)
{
    char output_string[18] = { 0 };
    memset(output_string, 0, sizeof(output_string));
    wifi_hal_dbg_print("%d %s \n", __LINE__, __func__);

    wifi_getSSIDMACAddress(apIndex, output_string);
    if (g_callbacks.apDeAuthEvent_cb) {
        wifi_hal_dbg_print("%d %s \n", __LINE__, __func__);
        g_callbacks.apDeAuthEvent_cb(apIndex, MAC, output_string, WIFI_MGMT_FRAME_TYPE_DEAUTH,
            reason);
    }
    return 0;
}

INT wifi_apDisAssocEvent_cb_func(INT apIndex, char *MAC, INT reason)
{
    char output_string[18] = { 0 };
    memset(output_string, 0, sizeof(output_string));
    wifi_hal_dbg_print("%d %s \n", __LINE__, __func__);

    wifi_getSSIDMACAddress(apIndex, output_string);
    if (g_callbacks.apDeAuthEvent_cb) {
        wifi_hal_dbg_print("%d %s \n", __LINE__, __func__);
        g_callbacks.disassoc_cb(apIndex, MAC, output_string, WIFI_MGMT_FRAME_TYPE_DISASSOC, reason);
    }
    return 0;
}

void wifi_hal_newApAssociatedDevice_callback_register(
    wifi_newApAssociatedDevice_callback callback_proc)
{
    wifi_newApAssociatedDevice_callback_register(callback_proc);
    wifi_hal_dbg_print("%d %s \n", __LINE__, __func__);
}

void wifi_hal_apDeAuthEvent_callback_register(wifi_device_deauthenticated_callback callback_proc)
{
    wifi_hal_dbg_print("%d %s \n", __LINE__, __func__);
    g_callbacks.apDeAuthEvent_cb = callback_proc;
    wifi_apDeAuthEvent_callback_register(wifi_apDeAuthEvent_cb_func);
}

void wifi_hal_apDisassociatedDevice_callback_register(
    wifi_device_disassociated_callback callback_proc)
{
    wifi_hal_dbg_print("%d %s \n", __LINE__, __func__);
    g_callbacks.disassoc_cb = callback_proc;
    wifi_apDisassociatedDevice_callback_register(wifi_apDisAssocEvent_cb_func);
}

void wifi_hal_radiusFallback_failover_callback_register(
    wifi_radiusFallback_failover_callback callback_proc)
{
}

void wifi_hal_stamode_callback_register(wifi_stamode_callback func)
{
}

void wifi_hal_set_mgt_frame_rate_limit(bool enable, int rate_limit, int window_size,
    int cooldown_time)
{
}

void wifi_hal_apStatusCode_callback_register(wifi_apStatusCode_callback func)
{
}

int wifi_hal_add_station_bridge(char *interface_name, char *bridge_name)
{
    return RETURN_OK;
}

INT wifi_wpsEvent_callback_register(wifi_wpsEvent_callback func)
{
    return RETURN_OK;
}

INT wifi_hal_get_RegDomain(wifi_radio_index_t radioIndex, UINT *reg_domain)
{
    return RETURN_OK;
}
