//
//  Copyright (C) 2026 Yusufcan Yavic
//  SPDX-License-Identifier: GPL-3.0-or-later
//
/* YYavicAirWiFiLayer kmod info (Copyright (C) 2026 Yusufcan Yavic, GPL-3.0-or-later) */
#include <mach/mach_types.h>

extern kern_return_t _start(kmod_info_t *ki, void *data);
extern kern_return_t _stop(kmod_info_t *ki, void *data);

__attribute__((visibility("default"))) KMOD_EXPLICIT_DECL(com.yyavic.AirWiFiLayer, "0.4.6", _start, _stop)
__private_extern__ kmod_start_func_t *_realmain = 0;
__private_extern__ kmod_stop_func_t *_antimain = 0;
__private_extern__ int _kext_apple_cc = __APPLE_CC__ ;
