# OneWifi HAL Adapter

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)

## Overview

OneWifi HAL Adapter is a compatibility layer designed to bridge the gap between OneWifi and platforms that do not fully support netlink-based WiFi HAL implementations. This adapter enables seamless integration of OneWifi with legacy vendor HAL implementations, eliminating the need for complete HAL rewrites during software migrations.

## Architecture

```
┌─────────────────────┐
│     OneWifi         │
│  (WiFi Management)  │
└──────────┬──────────┘
           │
           │ OneWifi API Calls
           │
┌──────────▼──────────┐
│  OneWifi HAL        │
│     Adapter         │ ◄── This Library
│   (Translation)     │
└──────────┬──────────┘
           │
           │ Vendor HAL API Calls
           │
┌──────────▼──────────┐
│   Vendor HAL        │
│ (Hardware Specific) │
└─────────────────────┘
```

## Who Should Use This?

This library is essential for:
- **MSOs (Multiple System Operators)** who wish to use OneWifi with non-netlink based vendor HAL implementations
- **Device Manufacturers** with existing proprietary WiFi HAL APIs that need OneWifi compatibility
- **Platform Integrators** working on RDK-based systems where netlink-based HAL is not available or suitable

**⚠️ Important**: MSOs adopting OneWifi with non-netlink HAL implementations **must** build and integrate this library for OneWifi to function properly with their hardware.

## Prerequisites

- **InterfaceMap.json** - WiFi interface mapping configuration
- **OnewifiHalAdapterDataStore.json** - Default adapter data store
- **DISTRO Features** - Required DISTRO flags: `OneWifi`, `halVersion3`, `onewifi_hal_adapter`, `wps_support`, `sm_app`

## Project Structure

```
onewifi-hal-adapter/
├── include/               # Public header files
│   ├── common.h          # Common definitions
│   ├── ieee802_11_defs.h # IEEE 802.11 standard definitions
│   ├── ieee80211.h       # IEEE 802.11 protocol structures
│   ├── wifi_hal_rdk.h    # RDK WiFi HAL interface
│   └── wifi_hal_rdk_framework.h  # RDK framework definitions
├── src/                  # Source implementation
│   ├── wifi_hal_adapter.c      # Main adapter implementation
│   ├── wifi_hal_adapter.h      # Adapter internal headers
│   ├── wifi_hal_adapter_util.c # Utility functions
│   ├── Makefile.am       # Source build configuration
│   └── configure.ac      # Source configuration script
├── configure.ac          # Main autoconf configuration
├── Makefile.am          # Main makefile template
├── LICENSE              # Apache 2.0 license
├── COPYING              # License information
└── README.md            # This file
```

## Integration with OneWifi

Integrating the OneWifi HAL Adapter into your RDK-B platform requires several configuration steps:

### Step 1: Enable OneWifi Feature Flag

Enable the `OneWifi` feature flag in your distribution configuration. This flag impacts multiple components across RDK-B including:

- **GUI/WebUI**: Web interface updates for OneWifi management
- **CcspWifiAgent**: WiFi agent component updates
- **HAL Interface**: Hardware abstraction layer changes
- **TR-181 Data Model**: WiFi data model adaptations
- **System Configuration**: Platform-wide WiFi management changes
### Step 2: Configure Required DISTRO Features

Add the following DISTRO features to your platform configuration (typically in `conf/distro/include/*.inc`):

```bash
DISTRO_FEATURES_append = " OneWifi"
DISTRO_FEATURES_append = " halVersion3"
DISTRO_FEATURES_append = " onewifi_hal_adapter"
DISTRO_FEATURES_append = " wps_support"
DISTRO_FEATURES_append = " sm_app"
```

These features enable:
- **OneWifi**: Core OneWifi framework support
- **halVersion3**: WiFi HAL version 3 interface
- **onewifi_hal_adapter**: Adapter library integration
- **wps_support**: WiFi Protected Setup functionality
- **sm_app**: State machine application support

### Step 3: Create and Install Interface Map

The adapter requires an `InterfaceMap.json` configuration file that maps logical WiFi interfaces to physical hardware interfaces.

#### Creating InterfaceMap.json

Create a JSON file with your platform-specific interface mappings. Example structure:

```json
{
  "interfaces": [
    {
      "vap_index": 0,
      "radio_index": 0,
      "interface_name": "wlan0",
      "bridge_name": "brlan0",
      "vlan_id": 100
    },
    {
      "vap_index": 1,
      "radio_index": 0,
      "interface_name": "wlan0-1",
      "bridge_name": "brlan0",
      "vlan_id": 101
    },
        {
      "vap_index": 2,
      "radio_index": 1,
      "interface_name": "wlan1",
      "bridge_name": "brlan0",
      "vlan_id": 200
    }
  ]
}
```
#### Installing InterfaceMap.json

**Via BitBake Recipe**

The `onewifi-hal-adapter.bb` recipe automatically installs the interface map. Place your custom `InterfaceMap.json` in the recipe's file directory:

```bash
recipes-wifi/onewifi-hal-adapter/
├── onewifi-hal-adapter.bb
└── files/
    └── InterfaceMap.json
```

The recipe installs it to: `/etc/wlan/nvram/InterfaceMap.json`

### Step 4: Create and Install Adapter Default Data Store

The `OnewifiHalAdapterDataStore.json` file stores default factory settings and runtime configuration for WiFi interfaces. This file is critical for proper adapter initialization and default value provisioning.

#### Creating OnewifiHalAdapterDataStore.json
Create a JSON file with factory default settings for each WiFi interface. Example structure:

```json
[
  {
    "InterfaceName": "wlan0",
    "VapIndex": 0,
    "RadioIndex": 0,
    "VapEnable": "true",
    "RadioEnable": "true",
    "DefaultSSID": "MyHomeNetwork_2G",
    "DefaultPASSPHRASE": "MySecurePassword123",
    "MgmtFramePowerControl": "0",
    "SecureModeAkm": "psk2",
    "SecureModeMfp": "optional",
    "DefaultRadiusKey": "",
    "DefaultCountryCode": "US",
    "DefaultWpsPin": "12345670",
    "DefaultWifiPwd": "FactoryPassword123",
    "DefaultFactorySSID": "FactorySSID_2G"
  },
    {
    "InterfaceName": "wlan0-1",
    "VapIndex": 1,
    "RadioIndex": 0,
    "VapEnable": "false",
    "RadioEnable": "true",
    "DefaultSSID": "GuestNetwork_2G",
    "DefaultPASSPHRASE": "GuestPassword123",
    "MgmtFramePowerControl": "0",
    "SecureModeAkm": "psk2",
    "SecureModeMfp": "optional",
    "DefaultRadiusKey": "",
    "DefaultCountryCode": "US",
    "DefaultWpsPin": "",
    "DefaultWifiPwd": "",
    "DefaultFactorySSID": ""
  },
    {
    "InterfaceName": "wlan1",
    "VapIndex": 2,
    "RadioIndex": 1,
    "VapEnable": "true",
    "RadioEnable": "true",
    "DefaultSSID": "MyHomeNetwork_5G",
    "DefaultPASSPHRASE": "MySecurePassword123",
    "MgmtFramePowerControl": "0",
    "SecureModeAkm": "psk2",
    "SecureModeMfp": "optional",
    "DefaultRadiusKey": "",
    "DefaultCountryCode": "US",
    "DefaultWpsPin": "12345670",
    "DefaultWifiPwd": "FactoryPassword123",
    "DefaultFactorySSID": "FactorySSID_5G"
  }
]
```
#### Field Descriptions

| Field | Description | Example Values |
|-------|-------------|----------------|
| `InterfaceName` | Physical WiFi interface name | `wlan0`, `wlan1`, `wlan0-1` |
| `VapIndex` | Virtual Access Point index | `0`, `1`, `2` |
| `RadioIndex` | Radio hardware index (0=2.4GHz, 1=5GHz) | `0`, `1` |
| `VapEnable` | VAP enabled state | `"true"`, `"false"` |
| `RadioEnable` | Radio enabled state | `"true"`, `"false"` |
| `DefaultSSID` | Factory default SSID | Any valid SSID string |
| `DefaultPASSPHRASE` | Factory default passphrase | WPA2/3 passphrase (8-63 chars) |
| `MgmtFramePowerControl` | Management frame power in dBm | `"0"`, `"-3"`, `"3"` |
| `SecureModeAkm` | Authentication and Key Management | `"psk2"`, `"psk3"`, `"sae"`, `"wpa-enterprise"` |
| `SecureModeMfp` | Management Frame Protection | `"disabled"`, `"optional"`, `"required"` |
| `DefaultRadiusKey` | RADIUS shared secret (enterprise) | RADIUS key string or empty |
| `DefaultCountryCode` | WiFi regulatory domain | `"US"`, `"GB"`, `"DE"`, etc. |
| `DefaultWpsPin` | WPS PIN code | 8-digit PIN or empty |
| `DefaultWifiPwd` | Factory WiFi password | Factory default password |
| `DefaultFactorySSID` | Factory SSID for reset | SSID used on factory reset |

#### Installing OnewifiHalAdapterDataStore.json

The `onewifi-hal-adapter.bb` recipe automatically installs the data store. Place your custom `OnewifiHalAdapterDataStore.json` in the recipe's file directory:

```bash
recipes-wifi/onewifi-hal-adapter/
├── onewifi-hal-adapter.bb
└── files/
    ├── InterfaceMap.json
    └── OnewifiHalAdapterDataStore.json
```

The recipe installs it to: `/etc/wlan/nvram/OnewifiHalAdapterDataStore.json`
During runtime, the adapter will copy this to the secure location: `/nvram/secure/OnewifiHalAdapterDataStore.json`

### Runtime Updates via Driver Bringup Script

The adapter data store can be dynamically updated during driver initialization using the provided shell function. This is useful for provisioning device-specific settings during manufacturing or first boot.

**Example Driver Bringup Script:**

```bash
#!/bin/bash

CONFIG_FILE="/nvram/secure/OnewifiHalAdapterDataStore.json"

update_interface() {
    iface="$1"
    vap="$2"
    radio="$3"
    ssid="$4"
    pass="$5"
    mgmt="$6"
    akm="$7"
    mfp="$8"
    radiuskey="$9"
    countrycode="${10}"
    wpspin="${11}"
    factorypw="${12}"
    factoryssid="${13}"

    # Escape double quotes in values
    ssid_escaped=$(printf '%s' "$ssid" | sed 's/"/\\"/g')
    pass_escaped=$(printf '%s' "$pass" | sed 's/"/\\"/g')
    factorypw_escaped=$(printf '%s' "$factorypw" | sed 's/"/\\"/g')
    factoryssid_escaped=$(printf '%s' "$factoryssid" | sed 's/"/\\"/g')
        sed -i "/\"InterfaceName\": \"$iface\"/,/}/{
        s/\"VapEnable\": *\"[^\"]*\"/\"VapEnable\": \"$vap\"/
        s/\"RadioEnable\": *\"[^\"]*\"/\"RadioEnable\": \"$radio\"/
        s/\"DefaultSSID\": *\"[^\"]*\"/\"DefaultSSID\": \"$ssid_escaped\"/
        s/\"DefaultPASSPHRASE\": *\"[^\"]*\"/\"DefaultPASSPHRASE\": \"$pass_escaped\"/
        s/\"MgmtFramePowerControl\": *\"[^\"]*\"/\"MgmtFramePowerControl\": \"$mgmt\"/
        s/\"SecureModeAkm\": *\"[^\"]*\"/\"SecureModeAkm\": \"$akm\"/
        s/\"SecureModeMfp\": *\"[^\"]*\"/\"SecureModeMfp\": \"$mfp\"/
        s/\"DefaultRadiusKey\": *\"[^\"]*\"/\"DefaultRadiusKey\": \"$radiuskey\"/
        s/\"DefaultCountryCode\": *\"[^\"]*\"/\"DefaultCountryCode\": \"$countrycode\"/
        s/\"DefaultWpsPin\": *\"[^\"]*\"/\"DefaultWpsPin\": \"$wpspin\"/
        s/\"DefaultWifiPwd\": *\"[^\"]*\"/\"DefaultWifiPwd\": \"$factorypw_escaped\"/
        s/\"DefaultFactorySSID\": *\"[^\"]*\"/\"DefaultFactorySSID\": \"$factoryssid_escaped\"/
    }" "$CONFIG_FILE"
}

## Initialization Sequence

The OneWifi HAL Adapter follows a specific initialization sequence to ensure proper system bring-up:

### 1. Driver Installation
- Load vendor-specific WiFi drivers
- Initialize hardware interfaces
- Ensure driver modules are properly loaded and functional

### 2. Interface Creation
- Create WiFi network interfaces based on `InterfaceMap.json` configuration
- Map logical interfaces to physical radio hardware
- Configure interface naming and indexing per platform requirements

### 3. Database Bring-up
- **Default Database**: OneWifi uses `ovsdb-server` (Open vSwitch Database)
- **Version Storage**: Database schema version is stored in designated configuration file
- **Initialization**: 
  - Start OVSDB server instance
  - Load database schema
  - Initialize WiFi configuration tables
  - Persist database version for migration tracking

### 4. Migration Detection (Optional)
- **Purpose**: Detect transitions from CCSP to OneWifi or between software versions
- **Process**:
  - Check for existing configuration databases
  - Compare database schema versions
  - Identify legacy configuration sources (PSM, syscfg, etc.)
  - Determine if migration is required
- **Note**: This stage is optional and platform-dependent

### 5. OneWifi Bring-up
- Initialize OneWifi core components
- Load HAL adapter library (`libonewifi_hal_adapter.so`)
- Establish connection between OneWifi and vendor HAL
- Register callbacks for WiFi events
- Initialize radio and VAP configurations
- Start WiFi management services

### 6. Mesh Processes Bring-up
- Initialize mesh networking components (if enabled)
- Start mesh agent processes
- Configure mesh backhaul interfaces
- Establish mesh topology discovery
- Enable mesh routing protocols

### Initialization Flow Diagram

```
┌─────────────────────────┐
│  1. Driver Installation │
└───────────┬─────────────┘
            │
            ▼
┌─────────────────────────┐
│  2. Interface Creation  │
└───────────┬─────────────┘
            │
            ▼
┌─────────────────────────┐
│  3. Database Bring-up   │
│     (ovsdb-server)      │
└───────────┬─────────────┘
            │
            ▼
┌─────────────────────────┐
│ 4. Migration Detection  │
│      (Optional)         │
└───────────┬─────────────┘
            │
            ▼
┌─────────────────────────┐
│  5. OneWifi Bring-up    │
│   (HAL Adapter Load)    │
└───────────┬─────────────┘
            │
            ▼
┌─────────────────────────┐
│ 6. Mesh Processes       │
│      Bring-up           │
└─────────────────────────┘
```
## API Coverage

This adapter provides translation for:

- Radio management operations
- VAP (Virtual Access Point) configuration
- Client/station management
- WiFi security and authentication
- Channel management and scanning
- DPP (Device Provisioning Protocol)
- ANQP (Access Network Query Protocol)
- Statistics and diagnostics

## License

This project is licensed under the **Apache License 2.0**. See the [LICENSE](LICENSE) file for details.

```
Copyright 2025 Comcast Cable Communications Management, LLC

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
```

## Contributing

Contributions are welcome! Please ensure:

1. Code follows the existing style and conventions
2. All changes are properly tested with vendor HAL implementations
3. Documentation is updated for any API changes
4. Commits include clear descriptions of changes

## Support

For issues, questions, or contributions, please refer to the project's issue tracker on GitHub.

## Related Projects

- **OneWifi**: The unified WiFi management framework this adapter supports
- **RDK (Reference Design Kit)**: The broader ecosystem this library is designed for

---

**Note**: This is a critical component for OneWifi integration. Ensure proper testing with your vendor HAL implementation before deploying to production systems.