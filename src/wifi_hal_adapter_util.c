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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>

#include "wifi_hal_rdk_framework.h"

#if !defined(PLATFORM_LINUX)
char *to_mac_str (mac_address_t mac, mac_addr_str_t key) {
    snprintf(key, 18, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    return (char *)key;
}
#endif

#if !defined(PLATFORM_LINUX)
char *get_formatted_time(char *time)
{
    struct tm *tm_info;
    struct timeval tv_now;
    char tmp[128];

    gettimeofday(&tv_now, NULL);
    tm_info = localtime(&tv_now.tv_sec);

    strftime(tmp, 128, "%y%m%d-%T", tm_info);

    snprintf(time, 128, "%s.%06lu", tmp, tv_now.tv_usec);
    return time;
}
#endif

typedef enum {
      WIFI_BITRATE_1KBPS   = 10,
      WIFI_BITRATE_2KBPS   = 20,
      WIFI_BITRATE_5_5KBPS = 55,
      WIFI_BITRATE_6KBPS   = 60,
      WIFI_BITRATE_9KBPS   = 90,
      WIFI_BITRATE_11KBPS  = 110,
      WIFI_BITRATE_12KBPS  = 120,
      WIFI_BITRATE_18KBPS  = 180,
      WIFI_BITRATE_24KBPS  = 240,
      WIFI_BITRATE_36KBPS  = 360,
      WIFI_BITRATE_48KBPS  = 480,
      WIFI_BITRATE_54KBPS  = 540
 } wifi_hal_bitrate_t;
 
 struct wifiHalDataTxRateHalMap
{
    wifi_hal_bitrate_t  halDataTxRateEnum;
    char halDataTxRateStr[8];
};
 
 struct wifiHalDataTxRateHalMap wifiHalDataTxRateMap[] =
{
    {WIFI_BITRATE_1KBPS,   "1"},
    {WIFI_BITRATE_2KBPS,   "2"},
    {WIFI_BITRATE_5_5KBPS, "5.5"},
    {WIFI_BITRATE_6KBPS,   "6"},
    {WIFI_BITRATE_9KBPS,   "9"},
    {WIFI_BITRATE_11KBPS,  "11"},
    {WIFI_BITRATE_12KBPS,  "12"},
    {WIFI_BITRATE_18KBPS,  "18"},
    {WIFI_BITRATE_24KBPS,  "24"},
    {WIFI_BITRATE_36KBPS,  "36"},
    {WIFI_BITRATE_48KBPS,  "48"},
    {WIFI_BITRATE_54KBPS,  "54"}
};

int convert_string_to_int(int **int_list, char *val)
{
  int *list;
  int count;
  char *pos;
  //free(*int_list);
  *int_list = NULL;
  int i;

  pos = val; 
  count = 0; 
  while (*pos != '\0') {
    if (*pos == ',') 
      count++;
    pos++;
  }

  list = malloc(sizeof(int) * (count+2)); 
  if (list == NULL)
    return -1;
  pos = val; 
  count = 0; 
  char *new_token = strtok(pos, ",");
   while (new_token != NULL) {
    for (i = 0 ; i < ARRAY_SZ(wifiHalDataTxRateMap) ; ++i)
    if(!strcmp(new_token,wifiHalDataTxRateMap[i].halDataTxRateStr)) {
        list[count]=wifiHalDataTxRateMap[i].halDataTxRateEnum;
    }
    new_token = strtok(NULL, ",");
    count++;
  }
  list[count] = -1;
  *int_list = list;
  return 0;
}

int get_min_rate(int *list, float *min_mbr_rate) {

    int i = 0 , min_rate = 0;

    if (list == NULL) {
        return 0;
    }

    min_rate = list[0];

    for (i = 0; list[i] >= 0; i++) {
        if(min_rate > list[i]) {
            min_rate=list[i];
        }
    }
    *min_mbr_rate = (float)min_rate/10;
    return 0;
}

