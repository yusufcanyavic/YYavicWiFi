#!/bin/sh
# YYavicWiFi - one-time Bluetooth firmware setup for MediaTek MT7922.
# Copyright (C) 2026 Yusufcan Yavic. SPDX-License-Identifier: GPL-3.0-or-later
#
# macOS's bluetoothd loads the MT7922 Bluetooth firmware from /etc/bluetool.
# YYavicWiFi.kext (0.7.2+) does the USB ID mask and the NVRAM values itself;
# this script only copies the firmware file there. Run once:
#
#   sudo sh setup-bluetooth.sh
#
# BLUETOOL_DIR can point somewhere else for testing.

set -eu
FW=BT_RAM_CODE_MT7922_1_1_hdr.bin
SRC="$(cd "$(dirname "$0")" && pwd)/$FW"
DEST="${BLUETOOL_DIR:-/etc/bluetool}"

if [ ! -f "$SRC" ]; then
    echo "error: $FW not found next to this script" >&2
    exit 1
fi
if [ -z "${BLUETOOL_DIR:-}" ] && [ "$(id -u)" -ne 0 ]; then
    echo "Run with sudo:  sudo sh $0" >&2
    exit 1
fi

if [ -f "$DEST/$FW" ] && cmp -s "$SRC" "$DEST/$FW"; then
    echo "Firmware already installed: $DEST/$FW"
else
    mkdir -p "$DEST"
    cp "$SRC" "$DEST/$FW"
    chmod 644 "$DEST/$FW"
    echo "Installed: $DEST/$FW"
fi
echo "Restart the Mac (or wait ~30 s) and check: System Information -> Bluetooth (Chipset SRS_7922)."
