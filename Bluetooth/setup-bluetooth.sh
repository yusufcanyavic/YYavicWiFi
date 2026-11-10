#!/bin/sh
# YYavicWiFi - one-time Bluetooth setup for the MediaTek MT7922 (macOS 15 and 26).
# Copyright (C) 2026 Yusufcan Yavic. SPDX-License-Identifier: GPL-3.0-or-later
#
# YYavicWiFi.kext does the USB ID mask and the NVRAM values itself. This script
# does what a kext cannot:
#
#   1. copies the Bluetooth firmware to /etc/bluetool (needs sudo), and
#   2. on macOS 26 only: makes the one-byte change in BlueToolFixup (see
#      patch-bluetoolfixup.sh for what and why). macOS 15 does not need it.
#
# Run once:
#
#   sudo sh setup-bluetooth.sh [/path/to/BlueToolFixup.kext]
#
# On macOS 26 the BlueToolFixup.kext is the one in your OpenCore folder
# (EFI/OC/Kexts). Mount the EFI partition first; if exactly one mounted volume
# has EFI/OC/Kexts/BlueToolFixup.kext it is found automatically, otherwise pass
# the path. Then restart the Mac.
#
# BLUETOOL_DIR can point somewhere else for testing.

set -eu
FW=BT_RAM_CODE_MT7922_1_1_hdr.bin
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/$FW"
DEST="${BLUETOOL_DIR:-/etc/bluetool}"

if [ ! -f "$SRC" ]; then
    echo "error: $FW not found next to this script" >&2
    exit 1
fi
if [ -z "${BLUETOOL_DIR:-}" ] && [ "$(id -u)" -ne 0 ]; then
    echo "Run with sudo:  sudo sh $0" >&2
    exit 1
fi

# --- 1. firmware ---
if [ -f "$DEST/$FW" ] && cmp -s "$SRC" "$DEST/$FW"; then
    echo "Firmware already installed: $DEST/$FW"
else
    mkdir -p "$DEST"
    cp "$SRC" "$DEST/$FW"
    chmod 644 "$DEST/$FW"
    echo "Installed: $DEST/$FW"
fi

# --- 2. BlueToolFixup (macOS 26 = Darwin 25 and newer) ---
DARWIN_MAJOR=$(uname -r | cut -d. -f1)
if [ "$DARWIN_MAJOR" -lt 25 ]; then
    echo "macOS 15 or older: BlueToolFixup needs no change."
    echo "Restart the Mac (or wait ~30 s) and check: System Information -> Bluetooth (Chipset SRS_7922)."
    exit 0
fi

KEXT="${1:-${BTFIXUP_KEXT:-}}"
if [ -z "$KEXT" ]; then
    FOUND=""; N=0
    for k in /Volumes/*/EFI/OC/Kexts/BlueToolFixup.kext /Volumes/*/OC/Kexts/BlueToolFixup.kext; do
        [ -d "$k" ] && { FOUND="$k"; N=$((N + 1)); }
    done
    if [ "$N" -eq 1 ]; then
        KEXT="$FOUND"; echo "Found BlueToolFixup: $KEXT"
    else
        echo "" >&2
        echo "macOS 26: could not find BlueToolFixup.kext on a mounted EFI partition ($N matches)." >&2
        echo "Mount your EFI partition and run again with the path:" >&2
        echo "  sudo sh $0 /Volumes/EFI/EFI/OC/Kexts/BlueToolFixup.kext" >&2
        exit 4
    fi
fi

# Backups go to the home folder of the person who ran sudo.
if [ -n "${SUDO_USER:-}" ]; then
    HOME=$(eval echo "~$SUDO_USER"); export HOME
fi
sh "$HERE/patch-bluetoolfixup.sh" "$KEXT"
echo "Restart the Mac and check: System Information -> Bluetooth (Chipset SRS_7922)."
