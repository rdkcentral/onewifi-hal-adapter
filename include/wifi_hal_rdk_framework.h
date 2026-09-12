/*
 * If not stated otherwise in this file or this component's LICENSE file the
 *
 * Copyright 2018 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/


#ifndef _RDK_HAL_FRAMEWORK_H_
#define _RDK_HAL_FRAMEWORK_H_

#include "wifi_hal_rdk.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "wifi_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define     ARRAY_SZ(x)     (sizeof(x)/sizeof((x)[0]))

typedef struct {
    wifi_radio_index_t radioIndex;
    wifi_chan_eventType_t event;
    wifi_radar_eventType_t sub_event;
    UINT channel;
    wifi_channelBandwidth_t channelWidth;
    UINT op_class;
} __attribute__((__packed__)) wifi_channel_change_event_t;

typedef void (*wifi_chan_event_CB_t)(wifi_channel_change_event_t radio_channel_param);

typedef struct {
    unsigned int    vap_index;
    unsigned int    event;
    unsigned char   *wps_data;
} __attribute__((__packed__)) wifi_wps_event_t;

typedef struct {
    wifi_device_disassociated_callback      disassoc_cb;
    wifi_device_deauthenticated_callback    apDeAuthEvent_cb;
    wifi_chan_event_CB_t                    channel_change_event_callback;
} wifi_device_callbacks_t;

INT wifi_hal_init();
INT wifi_hal_pre_init();
INT wifi_hal_post_init(wifi_vap_info_map_t *vap_map);
INT wifi_hal_send_mgmt_frame_response(int ap_index, int type, int status, int status_code, uint8_t *frame, uint8_t *mac, int len, int rssi);
INT wifi_hal_getHalCapability(wifi_hal_capability_t *hal);
INT wifi_hal_connect(INT ap_index, wifi_bss_info_t *bss);
INT wifi_hal_setRadioOperatingParameters(wifi_radio_index_t index, wifi_radio_operationParam_t *operationParam);
INT wifi_hal_createVAP(wifi_radio_index_t index, wifi_vap_info_map_t *map);
INT wifi_hal_kickAssociatedDevice(INT ap_index, mac_address_t mac);
INT wifi_hal_startScan(wifi_radio_index_t index, wifi_neighborScanMode_t scan_mode, INT dwell_time, UINT num, UINT *chan_list);
INT wifi_hal_disconnect(INT ap_index);
INT wifi_hal_getRadioVapInfoMap(wifi_radio_index_t index, wifi_vap_info_map_t *map);
INT wifi_hal_setApWpsButtonPush(INT apIndex);
INT wifi_hal_setApWpsPin(INT ap_index, char *wps_pin);
INT wifi_hal_setApWpsCancel(INT ap_index);
INT wifi_hal_set_acs_keep_out_chans(wifi_radio_operationParam_t *wifi_radio_oper_param, int radioIndex);
INT wifi_hal_sendDataFrame(int vap_id, unsigned char *dmac, unsigned char *data_buff, int data_len, BOOL insert_llc, int protocal, int priority);
INT wifi_hal_addApAclDevice(INT apIndex, CHAR *DeviceMacAddress);
INT wifi_hal_delApAclDevice(INT apIndex, CHAR *DeviceMacAddress);
INT wifi_hal_delApAclDevices(INT apIndex);
INT wifi_hal_getRadioTransmitPower(INT radioIndex, ULONG *tx_power);
INT wifi_hal_startNeighborScan(INT apIndex, wifi_neighborScanMode_t scan_mode, INT dwell_time, UINT chan_num, UINT *chan_list);
INT wifi_hal_getNeighboringWiFiStatus(INT radioIndex, wifi_neighbor_ap2_t **neighbor_ap_array, UINT *output_array_size);
INT wifi_hal_configNeighborReports(UINT apIndex, bool enable, bool auto_resp);
INT wifi_hal_setNeighborReports(UINT apIndex, UINT numNeighborReports, wifi_NeighborReport_t *neighborReports);
INT wifi_hal_get_default_country_code(char *code);
INT wifi_hal_setApMacAddressControlMode(INT apIndex, INT filterMode);
INT wifi_hal_get_default_keypassphrase(char *password, int vap_index);
INT wifi_hal_get_default_ssid(char *ssid, int vap_index);
INT wifi_hal_get_default_radius_key(char *radius_key, int vap_index);
INT wifi_hal_get_default_wps_pin(char *pin);

int nvram_get_current_password(char *l_password, int vap_index);
int nvram_get_current_ssid(char *l_ssid, int vap_index);
int nvram_get_mgmt_frame_power_control(int vap_index, int* output_dbm);
int nvram_get_vap_enable_status(BOOL *vap_enable, int vap_index);
int nvram_get_radio_enable_status(BOOL *radio_enable, int radio_index);

bool is_db_upgrade_required(char *inactive_firmware);
void wifi_hal_newApAssociatedDevice_callback_register(wifi_newApAssociatedDevice_callback func);
void wifi_hal_apDisassociatedDevice_callback_register(wifi_device_disassociated_callback func);
void wifi_hal_stamode_callback_register(wifi_stamode_callback func);
void wifi_hal_radiusFallback_failover_callback_register(wifi_radiusFallback_failover_callback func);
void wifi_hal_apDeAuthEvent_callback_register(wifi_device_deauthenticated_callback func);
void wifi_hal_ap_max_client_rejection_callback_register(wifi_apMaxClientRejection_callback func);
void wifi_hal_radius_eap_failure_callback_register(wifi_radiusEapFailure_callback func);

INT wifi_chan_event_register(wifi_chan_event_CB_t event_cb);
void wifi_hal_apStatusCode_callback_register(wifi_apStatusCode_callback func);
void wifi_hal_register_frame_hook(wifi_hal_frame_hook_fn_t func);
INT wifi_vapstatus_callback_register(wifi_vapstatus_callback func);
INT wifi_hal_getRadioTemperature(wifi_radio_index_t radioIndex, 
                                 wifi_radioTemperature_t *radioPhyTemperature);
INT wifi_anqpSendResponse(UINT apIndex, mac_address_t sta, unsigned char token,
    wifi_anqp_node_t *head);
void wifi_hal_staConnectionStatus_callback_register(wifi_staConnectionStatus_callback func);
INT wifi_setGASConfiguration(UINT advertisementID, wifi_GASConfiguration_t *input_struct);
INT wifi_sendActionFrameExt(INT apIndex, mac_address_t MacAddr, UINT frequency, UINT wait,
    UCHAR *frame, UINT len);
int enablePassPointSettings(int ap_index, BOOL passpoint_enable, BOOL downstream_disable,
                             BOOL p2p_disable, BOOL layer2TIF);
INT wifi_anqp_request_callback_register(wifi_anqp_request_callback_t anqpReqCallback);
void wifi_hal_scanResults_callback_register(wifi_scanResults_callback func);
INT wifi_hal_getScanResults(wifi_radio_index_t index, wifi_channel_t *channel,
    wifi_bss_info_t **bss, UINT *num_bss);
void wifi_hal_set_neighbor_report(uint apIndex, uint add, mac_address_t mac);
int wifi_hal_add_station_bridge(char *interface_name, char *bridge_name);
INT wifi_wpsEvent_callback_register(wifi_wpsEvent_callback func);
INT wifi_hal_get_RegDomain(wifi_radio_index_t radioIndex, UINT *reg_domain);
#ifdef __cplusplus
}
#endif

#endif //_RDK_HAL_FRAMEWORK_H_
