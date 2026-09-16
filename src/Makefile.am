########################################################################
#
# Copyright 2025 Comcast Cable Communications Management, LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License
#########################################################################

AUTOMAKE_OPTIONS = subdir-objects
SUBDIRS = .

AM_CFLAGS = -D_ANSC_LINUX \
            -D_ANSC_USER \
            -D_ANSC_LITTLE_ENDIAN_ \
            -Wall -Werror -Wextra \
            -Wno-unused-parameter -Wno-pointer-sign -Wno-sign-compare \
            -Wno-type-limits -Wno-format-truncation

AM_CPPFLAGS = -I$(top_srcdir)/../include

hardware_platform = i686-linux-gnu

lib_LTLIBRARIES = libonewifi_hal_adapter.la

libonewifi_hal_adapter_la_CPPFLAGS = \
    -I$(top_srcdir)/include \
    -I${PKG_CONFIG_SYSROOT_DIR}${includedir}/ccsp \
    -I$(top_srcdir)/../include

libonewifi_hal_adapter_la_CFLAGS = $(AM_CFLAGS) \
    -Wno-poison-system-directories -Wno-discarded-qualifiers

if ONEWIFI_HAL
libonewifi_hal_adapter_la_SOURCES = \
    wifi_hal_adapter.c \
    wifi_hal_adapter_util.c
endif




