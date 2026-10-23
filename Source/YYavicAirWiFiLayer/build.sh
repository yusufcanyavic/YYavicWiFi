#!/bin/zsh
# Copyright (C) 2026 Yusufcan Yavic, SPDX-License-Identifier: GPL-3.0-or-later
# YYavicAirWiFiLayer: cekirdek bayraklariyla derle ve kext paketini olustur.
set -e
D=${0:A:h}; SDK=${MACKERNELSDK:-$D/../YYavicMtkWlan/MacKernelSDK}
XC=${DEVELOPER_DIR:-$(xcode-select -p)}
CXX=$(DEVELOPER_DIR=$XC xcrun -f clang++); CC=$(DEVELOPER_DIR=$XC xcrun -f clang)
KSDK=$(DEVELOPER_DIR=$XC xcrun --sdk macosx --show-sdk-path)
OUT=$D/build; rm -rf $OUT; mkdir -p $OUT/obj $OUT/YYavicAirWiFiLayer.kext/Contents/MacOS
FLAGS=(-target x86_64-apple-macos11.0 -mkernel -fapple-kext -nostdinc -fno-builtin -fno-common -O2 -g
       -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DPRIVATE -D__PRIVATE_SPI__ -DAPPLE -DNeXT
       -I$D/src -I$SDK/Headers -I$SDK/Headers/IOKit -isystem $KSDK/System/Library/Frameworks/Kernel.framework/Headers
       -fmacro-prefix-map=$D/../= -fdebug-prefix-map=$D/../=
       -Wno-everything -Werror=implicit-function-declaration)
$CXX -x c++ -std=gnu++17 -fno-exceptions -fno-rtti $FLAGS -c $D/src/YYavicAirWiFiLayer.cpp -o $OUT/obj/layer.o
$CC -x c ${FLAGS:#-fapple-kext} -c $D/src/YYavicAirWiFiLayer_info.c -o $OUT/obj/info.o
$CXX -target x86_64-apple-macos11.0 -nostdlib -Xlinker -kext -L$SDK/Library/x86_64 -L$KSDK/usr/lib \
     $OUT/obj/layer.o $OUT/obj/info.o -lkmodc++ -lkmod -lcc_kext -lkmod \
     -o $OUT/YYavicAirWiFiLayer.kext/Contents/MacOS/YYavicAirWiFiLayer
cp $D/Info.plist $OUT/YYavicAirWiFiLayer.kext/Contents/Info.plist
echo "kext: $OUT/YYavicAirWiFiLayer.kext"
