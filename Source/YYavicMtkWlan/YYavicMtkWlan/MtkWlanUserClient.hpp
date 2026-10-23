/*
 * Reworked by Yusufcan Yavic, 2026, for YYavicWiFi (native MT7922 Wi-Fi on macOS).
 * Starting point: the SimpleMtkWlan version of this file by laobamac.
 * Changed by Yusufcan Yavic for YYavicWiFi (see CHANGELOG.md).
 * See CHANGELOG.md. The original notices below remain in force; these
 * modifications are distributed under the same license as this file.
 */
//
//  MtkWlanUserClient.hpp
//  YYavicWiFi
//
//  Originally written by laobamac (王孝慈) on 2026/3/24 for SimpleMtkWlan.
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

#ifndef MtkWlanUserClient_hpp
#define MtkWlanUserClient_hpp

#include <IOKit/IOLib.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/network/IOEthernetInterface.h>
#include "YYavicMtkWlan.hpp"
#include <ClientKit/Common.h>

typedef IOReturn (*IOControlMethodAction)(OSObject * target, void *data, bool isSet);

class MtkWlanUserClient : public IOUserClient {
    
    OSDeclareDefaultStructors( MtkWlanUserClient );
    
public:
    
    virtual bool start( IOService * provider ) override;
    virtual void stop( IOService * provider ) override;
    virtual bool initWithTask( task_t owningTask, void * securityID,
    UInt32 type,  OSDictionary * properties ) override;
    virtual IOReturn clientDied (void) override;
    virtual IOReturn clientClose( void ) override;
    virtual IOReturn externalMethod( uint32_t selector, IOExternalMethodArguments * arguments, IOExternalMethodDispatch * dispatch = 0, OSObject * target = 0, void * reference = 0 ) override;
    
private:
    static IOReturn sDRIVER_INFO(OSObject* target, void* data, bool isSet);
    static IOReturn sSTA_INFO(OSObject* target, void* data, bool isSet);
    static IOReturn sPOWER(OSObject* target, void* data, bool isSet);
    static IOReturn sSTATE(OSObject* target, void* data, bool isSet);
    static IOReturn sNW_ID(OSObject* target, void* data, bool isSet);
    static IOReturn sWPA_KEY(OSObject* target, void* data, bool isSet);
    static IOReturn sASSOCIATE(OSObject* target, void* data, bool isSet);
    static IOReturn sDISASSOCIATE(OSObject* target, void* data, bool isSet);
    static IOReturn sJOIN(OSObject* target, void* data, bool isSet);
    static IOReturn sSCAN(OSObject* target, void* data, bool isSet);
    static IOReturn sSCAN_RESULT(OSObject* target, void* data, bool isSet);
    static IOReturn sTX_POWER_LEVEL(OSObject* target, void* data, bool isSet);
    static IOReturn sNW_BSSID(OSObject* target, void* data, bool isSet);
    static IOReturn sDBG(OSObject* target, void* data, bool isSet);
    static const IOControlMethodAction sMethods[IOCTL_ID_MAX];
    static IOReturn gateRun(OSObject *owner, void *a0, void *a1, void *a2, void *a3);
    
private:
    task_t fTask;
    YYavicMtkWlan *fDriver;
    IOEthernetInterface *fInf;
    struct _ifnet *fIfp;
    MtkDriverController *fDriverController;
    MtkDriverInfo *fDriverInfo;
    
protected:
    bool fScanResultWrapping;
    ieee80211_node *fNextNodeToSend;
};


#endif /* MtkWlanUserClient_hpp */
