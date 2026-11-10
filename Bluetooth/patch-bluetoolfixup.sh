#!/bin/sh
# YYavicWiFi - one-byte change in BlueToolFixup for the MT7922 Bluetooth on macOS 26.
# Copyright (C) 2026 Yusufcan Yavic. SPDX-License-Identifier: GPL-3.0-or-later
#
# Why: BlueToolFixup (Acidanthera, BSD-3-Clause) patches bluetoothd so that every
# USB Bluetooth device outside a small vendor list takes the generic USB path
# (cmp edx, 0xA5C; je  ->  cmp edx, 0xA5C; jmp). On macOS 26 that also cuts the
# MT7922 off from bluetoothd's own MediaTek path ("HCI Transport is set to USB SRS"):
# the chip never gets its power-on / firmware commands, HCI times out after 20 s.
# This script turns that one replacement byte back (0xEB -> 0x74), so the patch
# does nothing for this instruction. Nothing else in BlueToolFixup is touched.
#
# Usage (not as root; the kext is on your EFI partition or in a folder you own):
#
#   sh patch-bluetoolfixup.sh /path/to/BlueToolFixup.kext          apply
#   sh patch-bluetoolfixup.sh /path/to/BlueToolFixup.kext --check  only report
#   sh patch-bluetoolfixup.sh /path/to/BlueToolFixup.kext --revert undo
#
# A backup of the whole kext goes to ~/BlueToolFixup-backup-<date>/ first.
# It only changes the file when it finds the exact expected bytes exactly once;
# otherwise (other BlueToolFixup version) it leaves the file alone and says so.
# Mount the EFI partition first (e.g. with OpenCore Configurator or
# `sudo diskutil mount disk0s1`) and run it on the kext there, then reboot.

set -eu

KEXT="${1:-}"
MODE="${2:-apply}"

if [ -z "$KEXT" ]; then
    sed -n '2,/^set -eu/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//' >&2
    exit 1
fi
case "$MODE" in apply|--check|--revert) ;; *) echo "unknown option: $MODE" >&2; exit 1 ;; esac

EXE="$KEXT/Contents/MacOS/BlueToolFixup"
if [ ! -f "$EXE" ]; then
    echo "error: $EXE not found (pass the path of BlueToolFixup.kext)" >&2
    exit 1
fi
command -v perl >/dev/null 2>&1 || { echo "error: perl not found" >&2; exit 1; }

VERSION="?"
if [ -f "$KEXT/Contents/Info.plist" ]; then
    VERSION=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$KEXT/Contents/Info.plist" 2>/dev/null || echo "?")
fi

# Patch table entry: find  81fa5c0a000074 (cmp edx,0xA5C ; je)
#                    repl  81fa5c0a0000eb (cmp edx,0xA5C ; jmp)   <- original BlueToolFixup
#                    repl  81fa5c0a000074                          <- after this script
# Result of the scan: "orig" / "done" / "none" / "multi"
STATE=$(perl -e '
    local $/; open(F, "<:raw", $ARGV[0]) or die; my $d = <F>; close F;
    my $A = pack("H*", "81fa5c0a000074");
    my $o = $A . pack("H*", "81fa5c0a0000eb");
    my $n = $A . pack("H*", "81fa5c0a000074");
    my $co = () = $d =~ /\Q$o\E/g; my $cn = () = $d =~ /\Q$n\E/g;
    if ($co == 1 && $cn == 0) { print "orig" }
    elsif ($cn == 1 && $co == 0) { print "done" }
    elsif ($co + $cn == 0) { print "none" } else { print "multi" }
' "$EXE")

echo "BlueToolFixup $VERSION: $STATE"

case "$STATE" in
    orig) NOW="original (needs the change)" ;;
    done) NOW="already changed" ;;
    none) echo "The expected bytes are not in this BlueToolFixup ($VERSION). Not touching it." >&2
          echo "Tested with 2.7.2. Please report the version." >&2; exit 2 ;;
    *)    echo "More than one match; not touching it." >&2; exit 2 ;;
esac

if [ "$MODE" = "--check" ]; then echo "$NOW"; exit 0; fi

if [ "$MODE" = "apply" ] && [ "$STATE" = "done" ]; then echo "Nothing to do."; exit 0; fi
if [ "$MODE" = "--revert" ] && [ "$STATE" = "orig" ]; then echo "Already original, nothing to do."; exit 0; fi

BK="$HOME/BlueToolFixup-backup-$(date +%Y%m%d-%H%M%S)-$$"
mkdir -p "$BK"
cp -R "$KEXT" "$BK/"
echo "Backup: $BK"

if [ "$MODE" = "apply" ]; then FROM="eb"; TO="74"; else FROM="74"; TO="eb"; fi
perl -e '
    local $/; open(F, "+<:raw", $ARGV[0]) or die "open: $!"; my $d = <F>;
    my $A = pack("H*", "81fa5c0a000074");
    my $pair = $A . pack("H*", "81fa5c0a0000" . $ARGV[1]);
    my $i = index($d, $pair); die "pattern vanished" if $i < 0;
    my $pos = $i + 13;
    die "unexpected byte" unless substr($d, $pos, 1) eq pack("H2", $ARGV[1]);
    seek(F, $pos, 0); print F pack("H2", $ARGV[2]); close F;
' "$EXE" "$FROM" "$TO"

# Verify
AFTER=$(perl -e 'local $/; open(F,"<:raw",$ARGV[0]); my $d=<F>; my $A=pack("H*","81fa5c0a000074");
  print index($d, $A.pack("H*","81fa5c0a0000".$ARGV[1])) >= 0 ? "ok" : "bad"' "$EXE" "$TO")
if [ "$AFTER" != "ok" ]; then echo "error: verification failed; restore from $BK" >&2; exit 3; fi
echo "Done. Reboot to use it."
