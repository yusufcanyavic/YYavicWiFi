#!/bin/zsh
# YYavicWiFi.kext: MT7922 radio (YYavicMtkWlan) + native IO80211/Skywalk layer
# (YYavicAirWiFiLayer) in one kext. Copyright (C) 2026 Yusufcan Yavic, GPL-3.0-or-later.
#
#   build-yyavic.sh <tag>     e.g. YY_VERSION=0.7.0 ./build-yyavic.sh release
#
# The two parts are still compiled by their own builds (Xcode project for the
# radio, build.sh for the layer: different header sets), then linked together
# with one kmod_info and a merged Info.plist.
set -e
TAG=${1:?tag}
Y=${0:A:h}
# Xcode: DEVELOPER_DIR if set, else the selected one (xcode-select -p).
XC=${DEVELOPER_DIR:-$(xcode-select -p)}
# MacKernelSDK (acidanthera): the copy inside the radio project, or MACKERNELSDK.
SDKH=${MACKERNELSDK:-$Y/YYavicMtkWlan/MacKernelSDK}/Headers
OUT=$Y/build-$TAG
NAME=YYavicWiFi
BID=com.yyavic.WiFi
VER=${YY_VERSION:-0.5.0}

rm -rf $OUT; mkdir -p $OUT

# 1. radio objects
( cd $Y/YYavicMtkWlan && $XC/usr/bin/xcodebuild -project YYavicMtkWlan.xcodeproj -scheme YYavicMtkWlan \
    -configuration Debug HEADER_SEARCH_PATHS=$SDKH KERNEL_EXTENSION_HEADER_SEARCH_PATHS=$SDKH \
    GIT_COMMIT=-yyavic-$TAG SYMROOT=$OUT/radio OBJROOT=$OUT/radio/obj CODE_SIGNING_ALLOWED=NO \
    "OTHER_CFLAGS=\$(inherited) -fmacro-prefix-map=$Y/= -fdebug-prefix-map=$Y/=" build ) \
    > $OUT/radio.log 2>&1 || { grep -E "error:" $OUT/radio.log | head; exit 1; }
LFL=$(find $OUT/radio/obj -name YYavicMtkWlan.LinkFileList | head -1)
RINFO=$(find $OUT/radio -name YYavicMtkWlan.kext -type d | head -1)/Contents/Info.plist

# 2. layer object
( cd $Y/YYavicAirWiFiLayer && ./build.sh ) > $OUT/layer.log 2>&1 || { grep -E "error" $OUT/layer.log | head; exit 1; }
LOBJ=$Y/YYavicAirWiFiLayer/build/obj/layer.o

# 3. one kmod_info
CC=$(DEVELOPER_DIR=$XC xcrun -f clang); CXX=$(DEVELOPER_DIR=$XC xcrun -f clang++)
KSDK=$(DEVELOPER_DIR=$XC xcrun --sdk macosx --show-sdk-path)
cat > $OUT/info.c <<EOF
/* $NAME kmod info (Copyright (C) 2026 Yusufcan Yavic, GPL-3.0-or-later) */
#include <mach/mach_types.h>
extern kern_return_t _start(kmod_info_t *ki, void *data);
extern kern_return_t _stop(kmod_info_t *ki, void *data);
__attribute__((visibility("default"))) KMOD_EXPLICIT_DECL($BID, "$VER", _start, _stop)
__private_extern__ kmod_start_func_t *_realmain = 0;
__private_extern__ kmod_stop_func_t *_antimain = 0;
__private_extern__ int _kext_apple_cc = __APPLE_CC__ ;
EOF
$CC -x c -target x86_64-apple-macos11.0 -mkernel -nostdinc -fno-builtin -fno-common -O2 \
    -DKERNEL -I$SDKH -isystem $KSDK/System/Library/Frameworks/Kernel.framework/Headers \
    -c $OUT/info.c -o $OUT/info.o

# 4. link
grep -v '_info\.o$' $LFL > $OUT/objs.txt
echo $LOBJ >> $OUT/objs.txt
echo $OUT/info.o >> $OUT/objs.txt
K=$OUT/$NAME.kext
mkdir -p $K/Contents/MacOS
$CXX -target x86_64-apple-macos11.0 -nostdlib -Xlinker -kext -Xlinker -no_deduplicate \
    -L$SDKH/../Library/x86_64 -L$KSDK/usr/lib -filelist $OUT/objs.txt \
    -lkmodc++ -lkmod -lcc_kext -o $K/Contents/MacOS/$NAME 2> $OUT/link.log || { cat $OUT/link.log; exit 1; }

# 5. Info.plist: radio personality + layer personalities, union of libraries
python3 - "$RINFO" "$Y/YYavicAirWiFiLayer/Info.plist" "$K/Contents/Info.plist" "$NAME" "$BID" "$VER" <<'PY'
import plistlib, sys
r, l, out, name, bid, ver = sys.argv[1:]
R = plistlib.load(open(r, 'rb')); L = plistlib.load(open(l, 'rb'))
P = {}
for src in (R, L):
    for k, v in src.get('IOKitPersonalities', {}).items():
        v = dict(v); v['CFBundleIdentifier'] = bid; P[k] = v
def vt(s):
    return tuple(int(''.join(c for c in x if c.isdigit()) or 0) for x in s.split('.'))
libs = {}
for src in (R, L):
    for k, v in src.get('OSBundleLibraries', {}).items():
        if k not in libs or vt(v) > vt(libs[k]):
            libs[k] = v
D = {
    'CFBundleDevelopmentRegion': 'en', 'CFBundleExecutable': name, 'CFBundleIdentifier': bid,
    'CFBundleInfoDictionaryVersion': '6.0', 'CFBundleName': name, 'CFBundlePackageType': 'KEXT',
    'CFBundleShortVersionString': ver, 'CFBundleVersion': ver,
    'CFBundleSupportedPlatforms': ['MacOSX'], 'IOKitPersonalities': P, 'OSBundleLibraries': libs,
    'OSBundleRequired': 'Network-Root',
    'NSHumanReadableCopyright': 'Copyright (C) 2026 Yusufcan Yavic. GPL-3.0-or-later; includes GPL-2.0-or-later, BSD and ISC code (see CREDITS.md).',
}
plistlib.dump(D, open(out, 'wb'))
print('personalities:', ', '.join(P)); print('libraries:', libs)
PY
# no build-machine paths in the release binary (debug stabs)
strip -S $K/Contents/MacOS/$NAME
echo "kext: $K"
