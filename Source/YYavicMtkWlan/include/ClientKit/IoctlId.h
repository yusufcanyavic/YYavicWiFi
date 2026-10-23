/*
 * Reworked by Yusufcan Yavic, 2026, for YYavicWiFi (native MT7922 Wi-Fi on macOS).
 * Starting point: the SimpleMtkWlan version of this file by laobamac.
 * Added and changed by Yusufcan Yavic: debug command id.
 * See CHANGELOG.md. The original notices below remain in force; these
 * modifications are distributed under the same license as this file.
 */
//
//  IoctlId.h
//  YYavicWiFi
//
//  Originally written by laobamac (王孝慈) on 2026/3/12 for SimpleMtkWlan.
//  Copyright © 2026 laobamac (王孝慈). All rights reserved.
//

/*
* Copyright (C) 2020  钟先耀
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*/

#ifndef IoctlId_h
#define IoctlId_h

enum IOCTL_IDS {
    IOCTL_80211_DRIVER_INFO,
    IOCTL_80211_STA_INFO,
    IOCTL_80211_POWER,
    IOCTL_80211_STATE,
    IOCTL_80211_NW_ID,
    IOCTL_80211_WPA_KEY,
    IOCTL_80211_ASSOCIATE,
    IOCTL_80211_DISASSOCIATE,
    IOCTL_80211_JOIN,
    IOCTL_80211_SCAN,
    IOCTL_80211_SCAN_RESULT,
    IOCTL_80211_TX_POWER_LEVEL,
    IOCTL_80211_NW_BSSID,
    IOCTL_80211_DBG,
    
    IOCTL_ID_MAX
};

#endif /* IoctlId_h */
