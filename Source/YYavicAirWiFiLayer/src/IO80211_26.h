/* R9T: macOS 26.7.2 (25G332) BootKernelExtensions.kc vtable'larindan OTOMATIK uretildi (gen_headers.py).
 * Elle duzenleme yapmayin; yerlesim KC ile mangled duzeyde dogrulanir (mcompare.py). */
#pragma once
#include <IOKit/IOService.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/network/IOEthernetController.h>

/* uretilen siniflar */
class IOSkywalkInterface;
class IOSkywalkNetworkInterface;
class IOSkywalkEthernetInterface;
class IO80211SkywalkInterface;
class IO80211InfraInterface;
class IO80211InfraProtocol;
class IO80211Controller;
class IO80211WorkQueue;
/* ileri bildirimler (yalniz ad; tam tanim gerekirse elle eklenecek) */
struct CCPipe;
struct FlowIdMetadata;
struct IO80211APIUserClient;
struct IO80211CountryCodeOp;
struct IO80211FeatureCode;
struct IO80211FlowQueue;
struct IO80211FlowQueueHash;
struct IO80211FlowQueueLegacy;
struct IO80211InterfaceAVCAdvisory;
struct IO80211LinkState;
struct IO80211NetworkPacket;
struct IO80211Peer;
struct IO80211PeerManager;
struct IO80211VirtualInterface;
struct IOSkywalkLogicalLink;
struct IOSkywalkPacketBufferPool;
struct IOSkywalkPacketQueue;
struct PacketSkywalkScratch;
struct TxCompletionEnqueueStats;
struct TxSubmissionDequeueStats;
struct __ifnet;
struct __mbuf;
struct __va_list_tag;
struct appl80211_chip_diags_data;
struct appl80211_sleep_on_inactivity_config;
struct apple80211AssocCandidates;
struct apple80211ChannelInfo;
struct apple80211ScanRequest;
struct apple80211_ManagementInformationBasedot11_counters;
struct apple80211_apmode_data;
struct apple80211_associated_sleep_config;
struct apple80211_battery_ps_config;
struct apple80211_bcn_mute_config;
struct apple80211_beacon_info_t;
struct apple80211_beacon_msg;
struct apple80211_bg_motion_profile;
struct apple80211_bg_network;
struct apple80211_bg_params;
struct apple80211_bg_scan;
struct apple80211_bgscan_cached_network_data_list;
struct apple80211_btcoex_2g_chain_disable;
struct apple80211_btcoex_ext_profile;
struct apple80211_btcoex_max_nss_for_ap_data;
struct apple80211_btcoex_profile;
struct apple80211_btcoex_profile_active_data;
struct apple80211_bypass_tx_power_cap;
struct apple80211_capability_data;
struct apple80211_cca_report;
struct apple80211_channel;
struct apple80211_channel_data;
struct apple80211_channels_info;
struct apple80211_chip_counters_rx;
struct apple80211_chip_counters_tx;
struct apple80211_chip_error_counters_tx;
struct apple80211_chip_power_limit;
struct apple80211_chip_stats;
struct apple80211_congestion_control_indication;
struct apple80211_country_channel_data;
struct apple80211_country_code_data;
struct apple80211_crash_command;
struct apple80211_data_path_interface_stats;
struct apple80211_data_path_peer_stats;
struct apple80211_dbg_guard_time_params;
struct apple80211_debug_command;
struct apple80211_device_accessory_info;
struct apple80211_device_orientation;
struct apple80211_dhcp_renewal_data;
struct apple80211_drbg_entropy;
struct apple80211_dual_power_mode_params;
struct apple80211_dynamic_rssi_window_config;
struct apple80211_dynsar_detail;
struct apple80211_eap_filter_config;
struct apple80211_extended_bss_info;
struct apple80211_facetime_wificalling_params;
struct apple80211_fastlane;
struct apple80211_feature_flags;
struct apple80211_frame_counters;
struct apple80211_fw_clock_info;
struct apple80211_fw_clock_source;
struct apple80211_fw_hot_channels;
struct apple80211_gas_query_t;
struct apple80211_guard_interval_data;
struct apple80211_he_capability;
struct apple80211_he_counters_ctl;
struct apple80211_host_clock_info;
struct apple80211_hp2p_ctrl;
struct apple80211_ht_capability;
struct apple80211_hw_mac_address;
struct apple80211_ie_data;
struct apple80211_infra_enumerated;
struct apple80211_infra_specific_stats;
struct apple80211_interface_availability;
struct apple80211_interface_setting;
struct apple80211_ipv4_params;
struct apple80211_ipv6_params;
struct apple80211_key;
struct apple80211_latency_all_ac;
struct apple80211_le_scan_params;
struct apple80211_leaky_ap_setting;
struct apple80211_leave_network;
struct apple80211_legacy_roam_profile_config;
struct apple80211_limited_aggregation_config;
struct apple80211_lmtpc_config;
struct apple80211_low_latency_info;
struct apple80211_lqm_config_t;
struct apple80211_lqm_statistics;
struct apple80211_lqm_summary;
struct apple80211_mcs_data;
struct apple80211_mcs_vht_data;
struct apple80211_mimo_config;
struct apple80211_mimo_status;
struct apple80211_mitigate_interference;
struct apple80211_mws_accessory_power_limit;
struct apple80211_mws_antenna_selection;
struct apple80211_mws_condition_id_config;
struct apple80211_mws_rfem_config;
struct apple80211_mws_scan_freq;
struct apple80211_mws_scan_freq_mode;
struct apple80211_mws_time_sharing;
struct apple80211_mws_wifi_channel_bitmap;
struct apple80211_ndd_data;
struct apple80211_network_data;
struct apple80211_noise_per_ant_t;
struct apple80211_nss_data;
struct apple80211_offload_arp_data;
struct apple80211_offload_ndp_data;
struct apple80211_offload_tcpka_enable_t;
struct apple80211_opmode_data;
struct apple80211_os_eligibility;
struct apple80211_p2p_device_capability;
struct apple80211_platform_config;
struct apple80211_pm_mode;
struct apple80211_pmk;
struct apple80211_power_budget_t;
struct apple80211_power_data;
struct apple80211_power_debug_info;
struct apple80211_power_profile;
struct apple80211_powersave_data;
struct apple80211_powertable_version_data;
struct apple80211_private_mac_data;
struct apple80211_ranging_authenticate_request_t;
struct apple80211_ranging_capabilities_t;
struct apple80211_ranging_enable_request_t;
struct apple80211_ranging_start_request_t;
struct apple80211_rate_data;
struct apple80211_reassoc;
struct apple80211_reset_command;
struct apple80211_roam_cache_data;
struct apple80211_roam_profile_all_bands;
struct apple80211_roam_profile_config;
struct apple80211_rsdb_capability;
struct apple80211_rsn_xe_data;
struct apple80211_rssi_data;
struct apple80211_rx_data_stall_report;
struct apple80211_scan_data;
struct apple80211_sdb_enable;
struct apple80211_sensing_data_t;
struct apple80211_sensing_disable_t;
struct apple80211_sensing_enable_t;
struct apple80211_set_property_unserialized_data;
struct apple80211_set_roam_lock;
struct apple80211_sib_coex_status;
struct apple80211_slow_wifi_feature_enabled;
struct apple80211_smartcca_opmode;
struct apple80211_standalone_state;
struct apple80211_stat_report;
struct apple80211_state_data;
struct apple80211_sup_channel_data;
struct apple80211_system_sleep_config;
struct apple80211_thermal_index_t;
struct apple80211_timestamping_en;
struct apple80211_timesync_gpio;
struct apple80211_timesync_info;
struct apple80211_timesync_rx_policy;
struct apple80211_timesync_stats;
struct apple80211_timesync_tx_policy;
struct apple80211_tko_dump;
struct apple80211_tko_params;
struct apple80211_traffic_eng_params;
struct apple80211_trap_info_data;
struct apple80211_trap_mini_dump_data;
struct apple80211_tx_mode_config;
struct apple80211_txpower_data;
struct apple80211_txrx_chain_info;
struct apple80211_usb_host_notification_data;
struct apple80211_user_roam_cache;
struct apple80211_version_data;
struct apple80211_vht_capability;
struct apple80211_virt_if_create_data;
struct apple80211_voice_ind_state;
struct apple80211_wcl_abort_join;
struct apple80211_wcl_action_frame;
struct apple80211_wcl_advisory_info;
struct apple80211_wcl_arp_mode;
struct apple80211_wcl_low_latency_stats;
struct apple80211_wcl_qos_params;
struct apple80211_wcl_real_time_mode;
struct apple80211_wcl_traffic_counters;
struct apple80211_wcl_tx_rx_latency;
struct apple80211_wcl_ulofdma_state;
struct apple80211_wcl_update_link_state;
struct apple80211_wcl_wnm_config_t;
struct apple80211_wcl_wnm_offload_t;
struct apple80211_wifi_assertion_data;
struct apple80211_wme_ac;
struct apple80211_wow_low_power_mode;
struct apple80211_wow_test_data;
struct apple82011_postMessage_dps;
struct bss_blacklist;
struct ether_addr;
struct ether_header;
struct if_link_status;
struct ifdrv;
struct ifnet_init_eparams;
struct ifnet_traffic_descriptor_common;
struct mloAddrArray;
struct nicproxy_info_s;
struct nicproxy_limits_info_s;
struct packet_info_tag;
struct scanHomeAndAwayTime;
struct sockaddr_dl;
struct triggerCC;
struct userPrintCtx;

class IOSkywalkInterface : public IOService {
    OSDeclareAbstractStructors(IOSkywalkInterface)
public:
    virtual void free();  // slot 20 [override]
    virtual bool init(OSDictionary*);  // slot 75 [override]
    virtual bool willTerminate(IOService*, unsigned int);  // slot 130 [override]
    virtual bool didTerminate(IOService*, unsigned int, bool*);  // slot 131 [override]
    virtual bool handleOpen(IOService*, unsigned int, void*);  // slot 191 [override]
    virtual void handleClose(IOService*, unsigned int);  // slot 192 [override]
    virtual bool handleIsOpen(IOService const*) const;  // slot 193 [override]
    virtual IOReturn enable(unsigned int) = 0;  // slot 268 [saf]
    virtual IOReturn disable(unsigned int) = 0;  // slot 269 [saf]
    virtual IOReturn clientConnectWithTask(task*, IOService*, unsigned int);  // slot 270
    virtual void clientDisconnect(IOService*, unsigned int);  // slot 271
    virtual bool isTerminating();  // slot 272
    virtual IOReturn _RESERVEDIOSkywalkInterface0();  /* RET? */  // slot 273
    virtual IOReturn _RESERVEDIOSkywalkInterface1();  /* RET? */  // slot 274
    virtual IOReturn _RESERVEDIOSkywalkInterface2();  /* RET? */  // slot 275
    virtual IOReturn _RESERVEDIOSkywalkInterface3();  /* RET? */  // slot 276
    virtual IOReturn _RESERVEDIOSkywalkInterface4();  /* RET? */  // slot 277
    virtual IOReturn _RESERVEDIOSkywalkInterface5();  /* RET? */  // slot 278
    virtual IOReturn _RESERVEDIOSkywalkInterface6();  /* RET? */  // slot 279
    virtual IOReturn _RESERVEDIOSkywalkInterface7();  /* RET? */  // slot 280
    virtual IOReturn _RESERVEDIOSkywalkInterface8();  /* RET? */  // slot 281
    virtual IOReturn _RESERVEDIOSkywalkInterface9();  /* RET? */  // slot 282
    virtual IOReturn _RESERVEDIOSkywalkInterface10();  /* RET? */  // slot 283
private:
    uint8_t _r9t_pad[40];  // 26 classSize 176 - ust 136
};
static_assert(sizeof(IOSkywalkInterface) == 176, "IOSkywalkInterface boyutu macOS 26 ile uyusmuyor");

class IOSkywalkNetworkInterface : public IOSkywalkInterface {
    OSDeclareAbstractStructors(IOSkywalkNetworkInterface)
public:
    struct IOSkywalkTSOOptions;
    struct RegistrationInfo;
    virtual void free();  // slot 20 [override]
    virtual bool init(OSDictionary*);  // slot 75 [override]
    virtual bool willTerminate(IOService*, unsigned int);  // slot 130 [override]
    virtual void stop(IOService*);  // slot 187 [override]
    virtual void joinPMtree(IOService*);  // slot 246 [override]
    virtual IOReturn setAggressiveness(unsigned long, unsigned long);  // slot 250 [override]
    virtual IOReturn enable(unsigned int);  // slot 268 [override]
    virtual IOReturn disable(unsigned int);  // slot 269 [override]
    virtual IOReturn registerNetworkInterfaceWithLogicalLink(IOSkywalkNetworkInterface::RegistrationInfo const*, IOSkywalkLogicalLink*, IOSkywalkPacketBufferPool*, IOSkywalkPacketBufferPool*, unsigned int);  // slot 284
    virtual IOReturn deregisterLogicalLink();  // slot 285
    virtual SInt32 initBSDInterfaceParameters(ifnet_init_eparams*, sockaddr_dl**) = 0;  // slot 286 [saf]
    virtual bool prepareBSDInterface(__ifnet*, unsigned int);  // slot 287
    virtual void finalizeBSDInterface(__ifnet*, unsigned int);  // slot 288
    virtual ifnet_t getBSDInterface() const;  // slot 289
    virtual void setBSDName(char const*);  // slot 290
    virtual const char * getBSDName() const;  // slot 291
    virtual IOReturn processBSDCommand(__ifnet*, unsigned int, void*);  // slot 292
    virtual IOReturn processInterfaceCommand(ifdrv*);  // slot 293
    virtual IOReturn interfaceAdvisoryEnable(bool);  // slot 294
    virtual IOReturn setRxFlowSteering(unsigned int, ifnet_traffic_descriptor_common*, unsigned int);  /* RET? */  // slot 295
    virtual SInt32 setInterfaceEnable(bool);  // slot 296
    virtual SInt32 setRunningState(bool);  // slot 297
    virtual IOReturn handleChosenMedia(unsigned int);  // slot 298
    virtual void * getSupportedMediaArray(unsigned int*, unsigned int*);  // slot 299
    virtual void * getPacketTapInfo(unsigned int*, unsigned int*);  // slot 300
    virtual UInt getUnsentDataByteCount(unsigned int*, unsigned int*, unsigned int) const;  // slot 301
    virtual UInt32 getSupportedWakeFlags(unsigned int*);  // slot 302
    virtual void enableNetworkWake(unsigned int);  // slot 303
    virtual void calculateRingSizeForQueue(IOSkywalkPacketQueue const*, unsigned int*) const;  // slot 304
    virtual UInt getMaxTransferUnit();  // slot 305
    virtual void setMaxTransferUnit(unsigned int);  // slot 306
    virtual UInt getMinPacketSize();  // slot 307
    virtual UInt getHardwareAssists();  // slot 308
    virtual void setHardwareAssists(unsigned int, unsigned int);  // slot 309
    virtual void * getInterfaceFamily();  // slot 310
    virtual void * getInterfaceSubFamily();  // slot 311
    virtual UInt getInitialMedia();  // slot 312
    virtual UInt32 getFeatureFlags();  // slot 313
    virtual UInt getTxDataOffset();  // slot 314
    virtual UInt captureInterfaceState(unsigned int);  // slot 315
    virtual void restoreInterfaceState(unsigned int);  // slot 316
    virtual void setMTU(unsigned int);  // slot 317
    virtual bool bpfTap(unsigned int, unsigned int);  // slot 318
    virtual const char * getBSDNamePrefix();  // slot 319
    virtual UInt getBSDUnitNumber();  // slot 320
    virtual const char * classNameOverride();  // slot 321
    virtual void deferBSDAttach(bool);  // slot 322
    virtual void reportDetailedLinkStatus(if_link_status const*);  // slot 323
    virtual UInt getTSOOptions(IOSkywalkNetworkInterface::IOSkywalkTSOOptions*);  // slot 324
    virtual IOReturn _RESERVEDIOSkywalkNetworkInterface0();  /* RET? */  // slot 325
    virtual IOReturn _RESERVEDIOSkywalkNetworkInterface1();  /* RET? */  // slot 326
    virtual IOReturn _RESERVEDIOSkywalkNetworkInterface2();  /* RET? */  // slot 327
    virtual IOReturn _RESERVEDIOSkywalkNetworkInterface3();  /* RET? */  // slot 328
    virtual IOReturn _RESERVEDIOSkywalkNetworkInterface4();  /* RET? */  // slot 329
    virtual IOReturn _RESERVEDIOSkywalkNetworkInterface5();  /* RET? */  // slot 330
    virtual IOReturn _RESERVEDIOSkywalkNetworkInterface6();  /* RET? */  // slot 331
    virtual IOReturn _RESERVEDIOSkywalkNetworkInterface7();  /* RET? */  // slot 332
    virtual IOReturn _RESERVEDIOSkywalkNetworkInterface8();  /* RET? */  // slot 333
    virtual IOReturn _RESERVEDIOSkywalkNetworkInterface9();  /* RET? */  // slot 334
private:
    uint8_t _r9t_pad[48];  // 26 classSize 224 - ust 176
};
static_assert(sizeof(IOSkywalkNetworkInterface) == 224, "IOSkywalkNetworkInterface boyutu macOS 26 ile uyusmuyor");

class IOSkywalkEthernetInterface : public IOSkywalkNetworkInterface {
    OSDeclareAbstractStructors(IOSkywalkEthernetInterface)
public:
    struct RegistrationInfo;
    virtual void free();  // slot 20 [override]
    virtual bool init(OSDictionary*);  // slot 75 [override]
    virtual IOReturn newUserClient(task*, void*, unsigned int, OSDictionary*, IOUserClient**);  // slot 240 [override]
    virtual IOReturn setPowerState(unsigned long, IOService*);  // slot 255 [override]
    virtual IOReturn enable(unsigned int);  // slot 268 [override]
    virtual IOReturn initBSDInterfaceParameters(ifnet_init_eparams*, sockaddr_dl**);  // slot 286 [override]
    virtual bool prepareBSDInterface(__ifnet*, unsigned int);  // slot 287 [override]
    virtual errno_t processBSDCommand(__ifnet*, unsigned int, void*);  // slot 292 [override]
    virtual void * getPacketTapInfo(unsigned int*, unsigned int*);  // slot 300 [override]
    virtual void enableNetworkWake(unsigned int);  // slot 303 [override]
    virtual UInt32 getMaxTransferUnit();  // slot 305 [override]
    virtual UInt32 getMinPacketSize();  // slot 307 [override]
    virtual void * getInterfaceFamily();  // slot 310 [override]
    virtual void * getInterfaceSubFamily();  // slot 311 [override]
    virtual UInt getInitialMedia();  // slot 312 [override]
    virtual const char * getBSDNamePrefix();  // slot 319 [override]
    virtual IOReturn getHardwareAddress(ether_addr*);  // slot 335
    virtual void setHardwareAddress(ether_addr*);  // slot 336
    virtual void setLinkLayerAddress(ether_addr*);  // slot 337
    virtual bool configureMulticastFilter(unsigned int, ether_addr const*, unsigned int);  // slot 338
    virtual bool setMulticastAddresses(ether_addr const*, unsigned int);  // slot 339
    virtual void setAllMulticastModeEnable(bool);  // slot 340
    virtual IOReturn setPromiscuousModeEnable(bool, unsigned int);  // slot 341
    virtual void reportNicProxyLimits(nicproxy_limits_info_s);  // slot 342
    virtual void hwConfigNicProxyData(nicproxy_info_s*);  // slot 343
    virtual IOReturn _RESERVEDIOSkywalkEthernetInterface0();  /* RET? */  // slot 344
    virtual IOReturn _RESERVEDIOSkywalkEthernetInterface1();  /* RET? */  // slot 345
    virtual IOReturn _RESERVEDIOSkywalkEthernetInterface2();  /* RET? */  // slot 346
    virtual IOReturn _RESERVEDIOSkywalkEthernetInterface3();  /* RET? */  // slot 347
    virtual IOReturn _RESERVEDIOSkywalkEthernetInterface4();  /* RET? */  // slot 348
    virtual IOReturn _RESERVEDIOSkywalkEthernetInterface5();  /* RET? */  // slot 349
    virtual IOReturn _RESERVEDIOSkywalkEthernetInterface6();  /* RET? */  // slot 350
    virtual IOReturn _RESERVEDIOSkywalkEthernetInterface7();  /* RET? */  // slot 351
    virtual IOReturn _RESERVEDIOSkywalkEthernetInterface8();  /* RET? */  // slot 352
    virtual IOReturn _RESERVEDIOSkywalkEthernetInterface9();  /* RET? */  // slot 353
    virtual IOReturn _RESERVEDIOSkywalkEthernetInterface10();  /* RET? */  // slot 354
    bool initRegistrationInfo(RegistrationInfo*, unsigned int, unsigned long);  // sanal degil (YYavic)
private:
    uint8_t _r9t_pad[64];  // 26 classSize 288 - ust 224
};
static_assert(sizeof(IOSkywalkEthernetInterface) == 288, "IOSkywalkEthernetInterface boyutu macOS 26 ile uyusmuyor");

class IO80211SkywalkInterface : public IOSkywalkEthernetInterface {
    OSDeclareAbstractStructors(IO80211SkywalkInterface)
public:
    virtual bool init();  // slot 19 [override]
    virtual void free();  // slot 20 [override]
    virtual IOReturn configureReport(IOReportChannelList*, unsigned int, void*, void*);  // slot 135 [override]
    virtual IOReturn updateReport(IOReportChannelList*, unsigned int, void*, void*);  // slot 136 [override]
    virtual bool start(IOService*);  // slot 186 [override]
    virtual void stop(IOService*);  // slot 187 [override]
    virtual IOReturn newUserClient(task*, void*, unsigned int, OSDictionary*, IOUserClient**);  // slot 240 [override]
    virtual const char* stringFromReturn(int);  // slot 242 [override]
    virtual int errnoFromReturn(int);  // slot 243 [override]
    virtual IOReturn setPowerState(unsigned long, IOService*);  // slot 255 [override]
    virtual unsigned long maxCapabilityForDomainState(unsigned long);  // slot 256 [override]
    virtual unsigned long initialPowerStateForDomainState(unsigned long);  // slot 257 [override]
    virtual IOReturn enable(unsigned int);  // slot 268 [override]
    virtual IOReturn disable(unsigned int);  // slot 269 [override]
    virtual IOReturn initBSDInterfaceParameters(ifnet_init_eparams*, sockaddr_dl**);  // slot 286 [override]
    virtual bool prepareBSDInterface(__ifnet*, unsigned int);  // slot 287 [override]
    virtual errno_t processBSDCommand(__ifnet*, unsigned int, void*);  // slot 292 [override]
    virtual SInt32 setInterfaceEnable(bool);  // slot 296 [override]
    virtual SInt32 setRunningState(bool);  // slot 297 [override]
    virtual IOReturn handleChosenMedia(unsigned int);  // slot 298 [override]
    virtual void * getSupportedMediaArray(unsigned int*, unsigned int*);  // slot 299 [override]
    virtual UInt32 getFeatureFlags();  // slot 313 [override]
    virtual const char * classNameOverride();  // slot 321 [override]
    virtual IOReturn getHardwareAddress(ether_addr*);  // slot 335 [override]
    virtual void setHardwareAddress(ether_addr*);  // slot 336 [override]
    virtual IOReturn setPromiscuousModeEnable(bool, unsigned int);  // slot 341 [override]
    virtual void * createPeerManager();  // slot 355
    virtual IOReturn createPeer(unsigned char const*, IO80211PeerManager*);  /* RET? */  // slot 356
    virtual void postMessage(unsigned int, void*, unsigned long, bool);  // slot 357
    virtual IOReturn reportDataPathEvents(unsigned int, void*, unsigned long, bool);  // slot 358
    virtual IOReturn recordOutputPackets(TxSubmissionDequeueStats*, TxSubmissionDequeueStats*);  // slot 359
    virtual IOReturn recordOutputPacket(apple80211_wme_ac, int, int);  // slot 360
    virtual void logTxPacket(IO80211NetworkPacket*, PacketSkywalkScratch*, apple80211_wme_ac, bool);  // slot 361
    virtual void logTxCompletionPacket(IO80211NetworkPacket*, PacketSkywalkScratch*, unsigned char*, apple80211_wme_ac, int, unsigned int, bool, bool);  // slot 362
    virtual IOReturn recordCompletionPackets(TxCompletionEnqueueStats*, TxCompletionEnqueueStats*);  // slot 363
    virtual IOReturn inputPacket(IO80211NetworkPacket*, packet_info_tag*, ether_header*, bool*, bool);  // slot 364
    virtual IOReturn forwardInfraRelayPackets(IO80211NetworkPacket*, ether_header*);  // slot 365
    virtual void logSkywalkTxReqPacket(IO80211NetworkPacket*, PacketSkywalkScratch*, unsigned char*, apple80211_wme_ac, bool);  // slot 366
    virtual SInt64 pendingPackets(unsigned char);  // slot 367
    virtual SInt64 packetSpace(unsigned char);  // slot 368
    virtual bool isChipInterfaceReady();  // slot 369
    virtual bool isDebounceOnGoing();  // slot 370
    virtual IO80211LinkState linkState();  // slot 371
    virtual void setScanningState(unsigned int, bool, apple80211_scan_data*, int);  // slot 372
    virtual void setDataPathState(bool);  // slot 373
    virtual void * getScanManager();  // slot 374
    virtual IOReturn getController();  /* RET? */  // slot 375
    virtual void updateLinkParameters(apple80211_interface_availability*);  // slot 376
    virtual void updateInterfaceCoexRiskPct(unsigned long long);  // slot 377
    virtual void setLQM(unsigned long long);  // slot 378
    virtual void updateLinkStatus();  // slot 379
    virtual void updateLinkStatusGated();  // slot 380
    virtual void setInterfaceExtendedCCA(apple80211_channel, apple80211_cca_report*);  // slot 381
    virtual void setInterfaceCCA(apple80211_channel, int);  // slot 382
    virtual void setInterfaceNF(apple80211_channel, long long);  // slot 383
    virtual void setInterfaceOFDMDesense(apple80211_channel, long long);  // slot 384
    virtual void removePacketQueue(IO80211FlowQueueHash*);  // slot 385
    virtual void setDebugFlags(unsigned long long, unsigned int);  // slot 386
    virtual SInt64 debugFlags();  // slot 387
    virtual void setInterfaceChipCounters(apple80211_stat_report*, apple80211_chip_counters_tx*, apple80211_chip_error_counters_tx*, apple80211_chip_counters_rx*);  // slot 388
    virtual void setInterfaceMIBdot11(apple80211_stat_report*, apple80211_ManagementInformationBasedot11_counters*);  // slot 389
    virtual void setFrameStats(apple80211_stat_report*, apple80211_frame_counters*);  // slot 390
    virtual void setInfraSpecificFrameStats(apple80211_stat_report*, apple80211_infra_specific_stats*);  // slot 391
    virtual IOReturn setRxDataStallStats(apple80211_stat_report*, apple80211_rx_data_stall_report*);  /* RET? */  // slot 392
    virtual SInt64 getWmeTxCounters(unsigned long long*);  // slot 393
    virtual void setPeerManagerLogFlag(unsigned int, unsigned int, unsigned int);  // slot 394
    virtual void setWoWEnabled(bool);  // slot 395
    virtual bool wowEnabled();  // slot 396
    virtual void printDataPath(userPrintCtx*);  // slot 397
    virtual UInt32 getDataQueueDepth();  // slot 398
    virtual bool findOrCreateFlowQueue(IO80211FlowQueueHash);  // slot 399
    virtual UInt64 findOrCreateFlowQueueWithCache(IO80211FlowQueueHash, bool*);  // slot 400
    virtual UInt64 findExistingFlowQueue(IO80211FlowQueueHash);  // slot 401
    virtual void removePacketQueue(IO80211FlowQueueHash const*);  // slot 402
    virtual void flushPacketQueues();  // slot 403
    virtual void cachePeer(ether_addr*, unsigned int*);  // slot 404
    virtual bool shouldLog(unsigned long long);  // slot 405
    virtual void vlogDebug(unsigned long long, char const*, __va_list_tag*);  // slot 406
    virtual void vlogDebugBPF(unsigned long long, char const*, __va_list_tag*);  // slot 407
    virtual UInt64 createLinkQualityMonitor(IO80211Peer*, bool);  // slot 408
    virtual void releaseLinkQualityMonitor(IO80211Peer*);  // slot 409
    virtual void * getP2PSkywalkPeerMgr();  // slot 410
    virtual bool isCommandProhibited(int);  // slot 411
    virtual IOReturn findPeer(ether_addr&);  /* RET? */  // slot 412
    virtual void setNotificationProperty(OSSymbol const*, OSObject const*);  // slot 413
    virtual void * getWorkerMatchingDict(OSString*);  // slot 414
    virtual bool init(IOService*, ether_addr*);  // slot 415
    virtual bool isInterfaceEnabled();  // slot 416
    virtual ether_addr * getSelfMacAddr();  // slot 417
    virtual void * getPacketPool(OSString*);  // slot 418
    virtual void * getLogger() const;  // slot 419
    virtual IOReturn handleSIOCSIFADDR();  // slot 420
    virtual IOReturn debugHandler(apple80211_debug_command*);  // slot 421
    virtual void statsDump();  // slot 422
    virtual void powerOnNotification();  // slot 423
    virtual void powerOffNotification();  // slot 424
    virtual UInt64 getTxQueueDepth();  // slot 425
    virtual UInt64 getRxQueueCapacity();  // slot 426
    virtual void updateRxCounter(unsigned long long);  // slot 427
    virtual void * getMultiCastQueue();  // slot 428
    virtual int getAssocState();  // slot 429
    virtual void notifyQueueState(apple80211_wme_ac, unsigned short);  // slot 430
    virtual int getTxHeadroom();  // slot 431
    virtual void * getRxCompQueue();  // slot 432
    virtual void * getTxCompQueue();  // slot 433
    virtual void * getTxSubQueue(apple80211_wme_ac);  // slot 434
    virtual void * getTxPacketPool();  // slot 435
    virtual void * getRxPacketPool();  // slot 436
    virtual void enableDatapath();  // slot 437
    virtual void disableDatapath();  // slot 438
    virtual int getNumTxQueues();  // slot 439
    virtual void * getLQMSummary(apple80211_lqm_summary*);  // slot 440
    virtual int getEventPipeSize();  // slot 441
    virtual UInt64 createEventPipe(IO80211APIUserClient*);  // slot 442
    virtual void destroyEventPipe(IO80211APIUserClient*);  // slot 443
    virtual IOReturn setUserBufferInfo(IOMemoryDescriptor*, unsigned long long);  /* RET? */  // slot 444
    virtual void postMessageIOUC(char const*, unsigned int, void*, unsigned long);  // slot 445
    virtual bool isIOUCPipeOpened();  // slot 446
    virtual void * getRingMD(IO80211APIUserClient*, unsigned long long);  // slot 447
    virtual IOReturn attachPeer(ether_addr*);  /* RET? */  // slot 448
    virtual IOReturn detachPeer(ether_addr*);  /* RET? */  // slot 449
    virtual IOReturn setDebugTrafficReport(bool);  /* RET? */  // slot 450
    virtual IOReturn getDataPathInterfaceStats(apple80211_data_path_interface_stats*);  /* RET? */  // slot 451
    virtual IOReturn getDataPathPeerStats(apple80211_data_path_peer_stats*);  /* RET? */  // slot 452
    virtual IOReturn getLastQueuePacketTime(ether_addr*);  /* RET? */  // slot 453
    virtual IOReturn getLastRxUnicastLinkActivityTime(ether_addr*);  /* RET? */  // slot 454
    virtual IOReturn updateInterfaceDataStats(apple80211_data_path_interface_stats*);  /* RET? */  // slot 455
    virtual IOReturn updatePeerDataStats(apple80211_data_path_peer_stats*);  /* RET? */  // slot 456
    virtual IOReturn logTxLatency(unsigned char*, unsigned int, unsigned long long);  /* RET? */  // slot 457
    virtual IOReturn logRxLatency(unsigned int, unsigned long long);  /* RET? */  // slot 458
    virtual IOReturn getNClearTxRxLatency(apple80211_latency_all_ac*, apple80211_latency_all_ac*);  /* RET? */  // slot 459
    virtual IOReturn getLastTxTimeStamp(unsigned long long&);  /* RET? */  // slot 460
    virtual IOReturn getLastRxTimeStamp(unsigned long long&);  /* RET? */  // slot 461
    virtual IOReturn syncDPSStats(apple82011_postMessage_dps*);  /* RET? */  // slot 462
    void setInterfaceRole(unsigned int);  // sanal degil (YYavic)
    void setInterfaceId(unsigned int);  // sanal degil (YYavic)
    unsigned int getInterfaceRole();  // sanal degil (YYavic)
private:
    uint8_t _r9t_pad[8];  // 26 classSize 296 - ust 288
};
static_assert(sizeof(IO80211SkywalkInterface) == 296, "IO80211SkywalkInterface boyutu macOS 26 ile uyusmuyor");

class IO80211InfraInterface : public IO80211SkywalkInterface {
    OSDeclareAbstractStructors(IO80211InfraInterface)
public:
    virtual bool init();  // slot 19 [override]
    virtual void free();  // slot 20 [override]
    virtual IOReturn configureReport(IOReportChannelList*, unsigned int, void*, void*);  // slot 135 [override]
    virtual IOReturn updateReport(IOReportChannelList*, unsigned int, void*, void*);  // slot 136 [override]
    virtual bool start(IOService*);  // slot 186 [override]
    virtual void stop(IOService*);  // slot 187 [override]
    virtual IOReturn setPowerState(unsigned long, IOService*);  // slot 255 [override]
    virtual IOReturn initBSDInterfaceParameters(ifnet_init_eparams*, sockaddr_dl**);  // slot 286 [override]
    virtual bool prepareBSDInterface(__ifnet*, unsigned int);  // slot 287 [override]
    virtual errno_t processBSDCommand(__ifnet*, unsigned int, void*);  // slot 292 [override]
    virtual SInt32 setInterfaceEnable(bool);  // slot 296 [override]
    virtual UInt32 getHardwareAssists();  // slot 308 [override]
    virtual bool bpfTap(unsigned int, unsigned int);  // slot 318 [override]
    virtual void hwConfigNicProxyData(nicproxy_info_s*);  // slot 343 [override]
    virtual void postMessage(unsigned int, void*, unsigned long, bool);  // slot 357 [override]
    virtual IOReturn recordOutputPackets(TxSubmissionDequeueStats*, TxSubmissionDequeueStats*);  // slot 359 [override]
    virtual void logTxPacket(IO80211NetworkPacket*, PacketSkywalkScratch*, apple80211_wme_ac, bool);  // slot 361 [override]
    virtual void logTxCompletionPacket(IO80211NetworkPacket*, PacketSkywalkScratch*, unsigned char*, apple80211_wme_ac, int, unsigned int, bool, bool);  // slot 362 [override]
    virtual IOReturn recordCompletionPackets(TxCompletionEnqueueStats*, TxCompletionEnqueueStats*);  // slot 363 [override]
    virtual IOReturn inputPacket(IO80211NetworkPacket*, packet_info_tag*, ether_header*, bool*, bool);  // slot 364 [override]
    virtual SInt64 pendingPackets(unsigned char);  // slot 367 [override]
    virtual SInt64 packetSpace(unsigned char);  // slot 368 [override]
    virtual bool isDebounceOnGoing();  // slot 370 [override]
    virtual IO80211LinkState linkState();  // slot 371 [override]
    virtual void setScanningState(unsigned int, bool, apple80211_scan_data*, int);  // slot 372 [override]
    virtual void setDataPathState(bool);  // slot 373 [override]
    virtual void * getScanManager();  // slot 374 [override]
    virtual void updateLinkParameters(apple80211_interface_availability*);  // slot 376 [override]
    virtual void updateInterfaceCoexRiskPct(unsigned long long);  // slot 377 [override]
    virtual void setLQM(unsigned long long);  // slot 378 [override]
    virtual void updateLinkStatus();  // slot 379 [override]
    virtual void updateLinkStatusGated();  // slot 380 [override]
    virtual void setInterfaceExtendedCCA(apple80211_channel, apple80211_cca_report*);  // slot 381 [override]
    virtual void setInterfaceCCA(apple80211_channel, int);  // slot 382 [override]
    virtual void setInterfaceNF(apple80211_channel, long long);  // slot 383 [override]
    virtual void setInterfaceOFDMDesense(apple80211_channel, long long);  // slot 384 [override]
    virtual void setDebugFlags(unsigned long long, unsigned int);  // slot 386 [override]
    virtual SInt64 debugFlags();  // slot 387 [override]
    virtual void setInterfaceChipCounters(apple80211_stat_report*, apple80211_chip_counters_tx*, apple80211_chip_error_counters_tx*, apple80211_chip_counters_rx*);  // slot 388 [override]
    virtual void setInterfaceMIBdot11(apple80211_stat_report*, apple80211_ManagementInformationBasedot11_counters*);  // slot 389 [override]
    virtual void setFrameStats(apple80211_stat_report*, apple80211_frame_counters*);  // slot 390 [override]
    virtual void setInfraSpecificFrameStats(apple80211_stat_report*, apple80211_infra_specific_stats*);  // slot 391 [override]
    virtual IOReturn setRxDataStallStats(apple80211_stat_report*, apple80211_rx_data_stall_report*);  // slot 392 [override]
    virtual SInt64 getWmeTxCounters(unsigned long long*);  // slot 393 [override]
    virtual void setPeerManagerLogFlag(unsigned int, unsigned int, unsigned int);  // slot 394 [override]
    virtual void setWoWEnabled(bool);  // slot 395 [override]
    virtual bool wowEnabled();  // slot 396 [override]
    virtual UInt64 createLinkQualityMonitor(IO80211Peer*, bool);  // slot 408 [override]
    virtual void releaseLinkQualityMonitor(IO80211Peer*);  // slot 409 [override]
    virtual int getAssocState();  // slot 429 [override]
    virtual void * getLQMSummary(apple80211_lqm_summary*);  // slot 440 [override]
    virtual bool setLinkState(IO80211LinkState, unsigned int, bool, unsigned int, unsigned int);  // slot 463
    virtual IOReturn setLinkStateInternal(IO80211LinkState, unsigned int, bool, unsigned int, unsigned int);  // slot 464
    virtual IOReturn setCurrentApAddress(ether_addr*);  /* RET? */  // slot 465
    virtual void setWCL_ADVISORTY_INFO(apple80211_wcl_advisory_info*);  // slot 466
    virtual void * getWCL_TX_RX_LATENCY(apple80211_wcl_tx_rx_latency*);  // slot 467
    virtual IOReturn setWCL_LINK_STATE_UPDATE(apple80211_wcl_update_link_state*);  // slot 468
    virtual IOReturn createLQMData();  /* RET? */  // slot 469
    virtual IOReturn setMacAddress(mloAddrArray&) = 0;  /* RET? */  // slot 470 [saf]
private:
    uint8_t _r9t_pad[8];  // 26 classSize 304 - ust 296
};
static_assert(sizeof(IO80211InfraInterface) == 304, "IO80211InfraInterface boyutu macOS 26 ile uyusmuyor");

class IO80211InfraProtocol : public IO80211InfraInterface {
    OSDeclareAbstractStructors(IO80211InfraProtocol)
public:
    virtual IOReturn getCHANNEL(apple80211_channel_data*) = 0;  // slot 471 [saf]
    virtual IOReturn getPOWERSAVE(apple80211_powersave_data*) = 0;  // slot 472 [saf]
    virtual IOReturn getTXPOWER(apple80211_txpower_data*) = 0;  // slot 473 [saf]
    virtual IOReturn getRATE(apple80211_rate_data*) = 0;  // slot 474 [saf]
    virtual IOReturn getOP_MODE(apple80211_opmode_data*) = 0;  // slot 475 [saf]
    virtual IOReturn getRSSI(apple80211_rssi_data*) = 0;  // slot 476 [saf]
    virtual IOReturn getSUPPORTED_CHANNELS(apple80211_sup_channel_data*) = 0;  // slot 477 [saf]
    virtual IOReturn getGUARD_INTERVAL(apple80211_guard_interval_data*) = 0;  // slot 478 [saf]
    virtual IOReturn getMCS(apple80211_mcs_data*) = 0;  // slot 479 [saf]
    virtual IOReturn getPOWER_DEBUG_INFO(apple80211_power_debug_info*) = 0;  // slot 480 [saf]
    virtual IOReturn getHT_CAPABILITY(apple80211_ht_capability*) = 0;  // slot 481 [saf]
    virtual IOReturn getMCS_VHT(apple80211_mcs_vht_data*) = 0;  // slot 482 [saf]
    virtual IOReturn getCHANNELS_INFO(apple80211_channels_info*) = 0;  // slot 483 [saf]
    virtual IOReturn getVHT_CAPABILITY(apple80211_vht_capability*) = 0;  // slot 484 [saf]
    virtual IOReturn getROAM_PROFILE(apple80211_roam_profile_all_bands*) = 0;  // slot 485 [saf]
    virtual IOReturn getCHIP_COUNTER_STATS(apple80211_chip_stats*) = 0;  // slot 486 [saf]
    virtual IOReturn getDBG_GUARD_TIME_PARAMS(apple80211_dbg_guard_time_params*) = 0;  // slot 487 [saf]
    virtual IOReturn getLEAKY_AP_STATS_MODE(apple80211_leaky_ap_setting*) = 0;  // slot 488 [saf]
    virtual IOReturn getCOUNTRY_CHANNELS(apple80211_country_channel_data*) = 0;  // slot 489 [saf]
    virtual IOReturn getPRIVATE_MAC(apple80211_private_mac_data*) = 0;  // slot 490 [saf]
    virtual IOReturn getRANGING_ENABLE(apple80211_ranging_enable_request_t*) = 0;  // slot 491 [saf]
    virtual IOReturn getRANGING_START(apple80211_ranging_start_request_t*) = 0;  // slot 492 [saf]
    virtual IOReturn getAWDL_RSDB_CAPS(apple80211_rsdb_capability*) = 0;  // slot 493 [saf]
    virtual IOReturn getTKO_PARAMS(apple80211_tko_params*) = 0;  // slot 494 [saf]
    virtual IOReturn getTKO_DUMP(apple80211_tko_dump*) = 0;  // slot 495 [saf]
    virtual IOReturn getHW_SUPPORTED_CHANNELS(apple80211_sup_channel_data*) = 0;  // slot 496 [saf]
    virtual IOReturn getBTCOEX_PROFILE(apple80211_btcoex_profile*) = 0;  // slot 497 [saf]
    virtual IOReturn getBTCOEX_PROFILE_ACTIVE(apple80211_btcoex_profile_active_data*) = 0;  // slot 498 [saf]
    virtual IOReturn getTRAP_INFO(apple80211_trap_info_data*) = 0;  // slot 499 [saf]
    virtual IOReturn getTHERMAL_INDEX(apple80211_thermal_index_t*) = 0;  // slot 500 [saf]
    virtual IOReturn getMAX_NSS_FOR_AP(apple80211_btcoex_max_nss_for_ap_data*) = 0;  // slot 501 [saf]
    virtual IOReturn getBTCOEX_2G_CHAIN_DISABLE(apple80211_btcoex_2g_chain_disable*) = 0;  // slot 502 [saf]
    virtual IOReturn getPOWER_BUDGET(apple80211_power_budget_t*) = 0;  // slot 503 [saf]
    virtual IOReturn getOFFLOAD_TCPKA_ENABLE(apple80211_offload_tcpka_enable_t*) = 0;  // slot 504 [saf]
    virtual IOReturn getRANGING_CAPS(apple80211_ranging_capabilities_t*) = 0;  // slot 505 [saf]
    virtual IOReturn getLQM_CONFIG(apple80211_lqm_config_t*) = 0;  // slot 506 [saf]
    virtual IOReturn getTRAP_CRASHTRACER_MINI_DUMP(apple80211_trap_mini_dump_data*) = 0;  // slot 507 [saf]
    virtual IOReturn getBEACON_INFO(apple80211_beacon_info_t*) = 0;  // slot 508 [saf]
    virtual IOReturn getCHIP_POWER_RANGE(apple80211_chip_power_limit*) = 0;  // slot 509 [saf]
    virtual IOReturn getNSS(apple80211_nss_data*) = 0;  // slot 510 [saf]
    virtual IOReturn getHW_ADDR(apple80211_hw_mac_address*) = 0;  // slot 511 [saf]
    virtual IOReturn getCHIP_DIAGS(appl80211_chip_diags_data*) = 0;  // slot 512 [saf]
    virtual IOReturn getHP2P_CTRL(apple80211_hp2p_ctrl*) = 0;  // slot 513 [saf]
    virtual IOReturn getBSS_BLACKLIST(bss_blacklist*) = 0;  /* RET? */  // slot 514 [saf]
    virtual IOReturn getTXRX_CHAIN_INFO(apple80211_txrx_chain_info*) = 0;  // slot 515 [saf]
    virtual IOReturn getMIMO_STATUS(apple80211_mimo_status*) = 0;  // slot 516 [saf]
    virtual IOReturn getCUR_PMK(apple80211_pmk*) = 0;  // slot 517 [saf]
    virtual IOReturn getDYNSAR_DETAIL(apple80211_dynsar_detail*) = 0;  // slot 518 [saf]
    virtual IOReturn getCOUNTRY_CHANNELS_INFO(apple80211_channels_info*) = 0;  // slot 519 [saf]
    virtual IOReturn getLQM_SUMMARY(apple80211_lqm_summary*) = 0;  // slot 520 [saf]
    virtual IOReturn getSLOW_WIFI_FEATURE_ENABLED(apple80211_slow_wifi_feature_enabled*) = 0;  // slot 521 [saf]
    virtual IOReturn getTIMESYNC_INFO(apple80211_timesync_info*) = 0;  // slot 522 [saf]
    virtual IOReturn getSENSING_DATA(apple80211_sensing_data_t*) = 0;  // slot 523 [saf]
    virtual IOReturn getWCL_FW_HOT_CHANNELS(apple80211_fw_hot_channels*) = 0;  // slot 524 [saf]
    virtual IOReturn getWCL_LOW_LATENCY_INFO(apple80211_low_latency_info*) = 0;  // slot 525 [saf]
    virtual IOReturn getWCL_BSS_INFO(apple80211_beacon_msg*) = 0;  // slot 526 [saf]
    virtual IOReturn getWCL_TRAFFIC_COUNTERS(apple80211_wcl_traffic_counters*) = 0;  // slot 527 [saf]
    virtual IOReturn getWCL_GET_TX_BLANKING_STATUS(unsigned int*) = 0;  // slot 528 [saf]
    virtual IOReturn getHE_COUNTERS(apple80211_he_counters_ctl*) = 0;  /* RET? */  // slot 529 [saf]
    virtual IOReturn getWCL_CHANNELS_INFO(apple80211ChannelInfo*) = 0;  // slot 530 [saf]
    virtual IOReturn getRSN_XE(apple80211_rsn_xe_data*) = 0;  // slot 531 [saf]
    virtual IOReturn getSIB_COEX_STATUS(apple80211_sib_coex_status*) = 0;  // slot 532 [saf]
    virtual IOReturn getWCL_EXTENDED_BSS_INFO(apple80211_extended_bss_info*) = 0;  // slot 533 [saf]
    virtual IOReturn getWCL_LOW_LATENCY_INFO_STATS(apple80211_wcl_low_latency_stats*) = 0;  // slot 534 [saf]
    virtual IOReturn getWCL_BGSCAN_CACHE_RESULT(apple80211_bgscan_cached_network_data_list*) = 0;  // slot 535 [saf]
    virtual IOReturn getWCL_WNM_OFFLOAD(apple80211_wcl_wnm_offload_t*) = 0;  /* RET? */  // slot 536 [saf]
    virtual IOReturn getWIFI_NOISE_PER_ANT(apple80211_noise_per_ant_t*) = 0;  // slot 537 [saf]
    virtual IOReturn getFW_CLOCK_INFO(apple80211_fw_clock_info*) = 0;  /* RET? */  // slot 538 [saf]
    virtual IOReturn getTIMESYNC_STATS(apple80211_timesync_stats*) = 0;  /* RET? */  // slot 539 [saf]
    virtual IOReturn getSYSTEM_SLEEP_CONFIG(apple80211_system_sleep_config*) = 0;  /* RET? */  // slot 540 [saf]
    virtual IOReturn getSMARTCCA_OPMODE(apple80211_smartcca_opmode*) = 0;  /* RET? */  // slot 541 [saf]
    virtual IOReturn getLQM_STATISTICS(apple80211_lqm_statistics*) = 0;  /* RET? */  // slot 542 [saf]
    virtual IOReturn getHE_CAPABILITY(apple80211_he_capability*) = 0;  // slot 543 [saf]
    virtual IOReturn getDEVICE_ORIENTATION(apple80211_device_orientation*) = 0;  /* RET? */  // slot 544 [saf]
    virtual IOReturn getACCESSORY_STATE(apple80211_device_accessory_info*) = 0;  /* RET? */  // slot 545 [saf]
    virtual IOReturn getP2P_DEVICE_CAPABILITY(apple80211_p2p_device_capability*) = 0;  /* RET? */  // slot 546 [saf]
    virtual IOReturn getPOWERTABLE_VERSION(apple80211_powertable_version_data*) = 0;  /* RET? */  // slot 547 [saf]
    virtual IOReturn setCIPHER_KEY(apple80211_key*) = 0;  // slot 548 [saf]
    virtual IOReturn setCHANNEL(apple80211_channel_data*) = 0;  // slot 549 [saf]
    virtual IOReturn setPOWERSAVE(apple80211_powersave_data*) = 0;  // slot 550 [saf]
    virtual IOReturn setTXPOWER(apple80211_txpower_data*) = 0;  // slot 551 [saf]
    virtual IOReturn setRATE(apple80211_rate_data*) = 0;  // slot 552 [saf]
    virtual IOReturn setIBSS_MODE(apple80211_network_data*) = 0;  // slot 553 [saf]
    virtual IOReturn setAP_MODE(apple80211_apmode_data*) = 0;  // slot 554 [saf]
    virtual IOReturn setIE(apple80211_ie_data*) = 0;  // slot 555 [saf]
    virtual IOReturn setWOW_TEST(apple80211_wow_test_data*) = 0;  // slot 556 [saf]
    virtual IOReturn setCLEAR_PMKSA_CACHE(void*) = 0;  /* RET? */  // slot 557 [saf]
    virtual IOReturn setVIRTUAL_IF_CREATE(apple80211_virt_if_create_data*) = 0;  // slot 558 [saf]
    virtual IOReturn setHT_CAPABILITY(apple80211_ht_capability*) = 0;  // slot 559 [saf]
    virtual IOReturn setOFFLOAD_ARP(apple80211_offload_arp_data*) = 0;  // slot 560 [saf]
    virtual IOReturn setOFFLOAD_NDP(apple80211_offload_ndp_data*) = 0;  // slot 561 [saf]
    virtual IOReturn setGAS_REQ(apple80211_gas_query_t*) = 0;  // slot 562 [saf]
    virtual IOReturn setVHT_CAPABILITY(apple80211_vht_capability*) = 0;  // slot 563 [saf]
    virtual IOReturn setROAM_PROFILE(apple80211_roam_profile_all_bands*) = 0;  // slot 564 [saf]
    virtual IOReturn setDBG_GUARD_TIME_PARAMS(apple80211_dbg_guard_time_params*) = 0;  // slot 565 [saf]
    virtual IOReturn setLEAKY_AP_STATS_MODE(apple80211_leaky_ap_setting*) = 0;  // slot 566 [saf]
    virtual IOReturn setPRIVATE_MAC(apple80211_private_mac_data*) = 0;  // slot 567 [saf]
    virtual IOReturn setRESET_CHIP(apple80211_reset_command*) = 0;  // slot 568 [saf]
    virtual IOReturn setCRASH(apple80211_crash_command*) = 0;  // slot 569 [saf]
    virtual IOReturn setRANGING_ENABLE(apple80211_ranging_enable_request_t*) = 0;  // slot 570 [saf]
    virtual IOReturn setRANGING_START(apple80211_ranging_start_request_t*) = 0;  // slot 571 [saf]
    virtual IOReturn setRANGING_AUTHENTICATE(apple80211_ranging_authenticate_request_t*) = 0;  // slot 572 [saf]
    virtual IOReturn setTKO_PARAMS(apple80211_tko_params*) = 0;  // slot 573 [saf]
    virtual IOReturn setBTCOEX_PROFILE(apple80211_btcoex_profile*) = 0;  // slot 574 [saf]
    virtual IOReturn setBTCOEX_PROFILE_ACTIVE(apple80211_btcoex_profile_active_data*) = 0;  // slot 575 [saf]
    virtual IOReturn setTHERMAL_INDEX(apple80211_thermal_index_t*) = 0;  // slot 576 [saf]
    virtual IOReturn setBTCOEX_2G_CHAIN_DISABLE(apple80211_btcoex_2g_chain_disable*) = 0;  // slot 577 [saf]
    virtual IOReturn setPOWER_BUDGET(apple80211_power_budget_t*) = 0;  // slot 578 [saf]
    virtual IOReturn setOFFLOAD_TCPKA_ENABLE(apple80211_offload_tcpka_enable_t*) = 0;  // slot 579 [saf]
    virtual IOReturn setLQM_CONFIG(apple80211_lqm_config_t*) = 0;  // slot 580 [saf]
    virtual IOReturn setDYNAMIC_RSSI_WINDOW_CONFIG(apple80211_dynamic_rssi_window_config*) = 0;  /* RET? */  // slot 581 [saf]
    virtual IOReturn setUSB_HOST_NOTIFICATION(apple80211_usb_host_notification_data*) = 0;  // slot 582 [saf]
    virtual IOReturn setHP2P_CTRL(apple80211_hp2p_ctrl*) = 0;  // slot 583 [saf]
    virtual IOReturn setBSS_BLACKLIST(bss_blacklist*) = 0;  /* RET? */  // slot 584 [saf]
    virtual IOReturn setSET_PROPERTY(apple80211_set_property_unserialized_data*) = 0;  // slot 585 [saf]
    virtual IOReturn setROAM_CACHE_UPDATE(apple80211_roam_cache_data*) = 0;  // slot 586 [saf]
    virtual IOReturn setPM_MODE(apple80211_pm_mode*) = 0;  // slot 587 [saf]
    virtual IOReturn setSET_WIFI_ASSERTION_STATE(apple80211_wifi_assertion_data*) = 0;  // slot 588 [saf]
    virtual IOReturn setREALTIME_QOS_MSCS(apple80211_state_data*) = 0;  // slot 589 [saf]
    virtual IOReturn setSENSING_ENABLE(apple80211_sensing_enable_t*) = 0;  // slot 590 [saf]
    virtual IOReturn setSENSING_DISABLE(apple80211_sensing_disable_t*) = 0;  // slot 591 [saf]
    virtual IOReturn setWCL_LEAVE_NETWORK(apple80211_leave_network*) = 0;  // slot 592 [saf]
    virtual IOReturn setWCL_REASSOC(apple80211_reassoc*) = 0;  // slot 593 [saf]
    virtual IOReturn setWCL_SET_ROAM_LOCK(apple80211_set_roam_lock*) = 0;  // slot 594 [saf]
    virtual IOReturn setWCL_LEGACY_ROAM_PROFILE_CONFIG(apple80211_legacy_roam_profile_config*) = 0;  /* RET? */  // slot 595 [saf]
    virtual IOReturn setWCL_ROAM_PROFILE_CONFIG(apple80211_roam_profile_config*) = 0;  // slot 596 [saf]
    virtual IOReturn setWCL_ROAM_USER_CACHE(apple80211_user_roam_cache*) = 0;  // slot 597 [saf]
    virtual IOReturn setWCL_SCAN_ABORT(void*) = 0;  // slot 598 [saf]
    virtual IOReturn setWCL_REAL_TIME_MODE(apple80211_wcl_real_time_mode*) = 0;  // slot 599 [saf]
    virtual IOReturn setWCL_ARP_MODE(apple80211_wcl_arp_mode*) = 0;  /* RET? */  // slot 600 [saf]
    virtual IOReturn setWCL_JOIN_ABORT(apple80211_wcl_abort_join*) = 0;  // slot 601 [saf]
    virtual IOReturn setWCL_TRIGGER_CC(triggerCC*) = 0;  // slot 602 [saf]
    virtual IOReturn setWCL_SCAN_REQ(apple80211ScanRequest*) = 0;  // slot 603 [saf]
    virtual IOReturn setWCL_ASSOCIATE(apple80211AssocCandidates*) = 0;  // slot 604 [saf]
    virtual IOReturn setWCL_QOS_PARAMS(apple80211_wcl_qos_params*) = 0;  /* RET? */  // slot 605 [saf]
    virtual IOReturn setWCL_LINK_UP_DONE(void*) = 0;  // slot 606 [saf]
    virtual IOReturn setWCL_SET_SCAN_HOME_AWAY_TIME(scanHomeAndAwayTime*) = 0;  // slot 607 [saf]
    virtual IOReturn setVOICE_IND_STATE(apple80211_voice_ind_state*) = 0;  /* RET? */  // slot 608 [saf]
    virtual IOReturn setRSN_XE(apple80211_rsn_xe_data*) = 0;  // slot 609 [saf]
    virtual IOReturn setMWS_ACCESSORY_POWER_LIMIT_WIFI_ENH(apple80211_mws_accessory_power_limit*) = 0;  /* RET? */  // slot 610 [saf]
    virtual IOReturn setWCL_ULOFDMA_STATE(apple80211_wcl_ulofdma_state*) = 0;  // slot 611 [saf]
    virtual IOReturn setWCL_ACTION_FRAME(apple80211_wcl_action_frame*) = 0;  // slot 612 [saf]
    virtual IOReturn setGAS_ABORT(void*) = 0;  // slot 613 [saf]
    virtual IOReturn setOS_FEATURE_FLAGS(apple80211_feature_flags*) = 0;  // slot 614 [saf]
    virtual IOReturn setDHCP_RENEWAL_DATA(apple80211_dhcp_renewal_data*) = 0;  // slot 615 [saf]
    virtual IOReturn setBATTERY_POWERSAVE_CONFIG(apple80211_battery_ps_config*) = 0;  // slot 616 [saf]
    virtual IOReturn setMIMO_CONFIG(apple80211_mimo_config*) = 0;  // slot 617 [saf]
    virtual IOReturn setWCL_CONFIG_BG_MOTIONPROFILE(apple80211_bg_motion_profile*) = 0;  // slot 618 [saf]
    virtual IOReturn setWCL_CONFIG_BG_NETWORK(apple80211_bg_network*) = 0;  // slot 619 [saf]
    virtual IOReturn setWCL_CONFIG_BGSCAN(apple80211_bg_scan*) = 0;  // slot 620 [saf]
    virtual IOReturn setWCL_CONFIG_BG_PARAMS(apple80211_bg_params*) = 0;  // slot 621 [saf]
    virtual IOReturn setPOWER_PROFILE(apple80211_power_profile*) = 0;  /* RET? */  // slot 622 [saf]
    virtual IOReturn setHEARTBEAT(void*) = 0;  /* RET? */  // slot 623 [saf]
    virtual IOReturn setINTERFACE_SETTING(apple80211_interface_setting*) = 0;  /* RET? */  // slot 624 [saf]
    virtual IOReturn setBYPASS_TX_POWER_CAP(apple80211_bypass_tx_power_cap*) = 0;  /* RET? */  // slot 625 [saf]
    virtual IOReturn setFACETIME_WIFICALLING_PARAMS(apple80211_facetime_wificalling_params*) = 0;  /* RET? */  // slot 626 [saf]
    virtual IOReturn setIPV4_PARAMS(apple80211_ipv4_params*) = 0;  /* RET? */  // slot 627 [saf]
    virtual IOReturn setWCL_WNM_OPS(apple80211_wcl_wnm_config_t*) = 0;  /* RET? */  // slot 628 [saf]
    virtual IOReturn setWCL_WNM_OFFLOAD(apple80211_wcl_wnm_offload_t*) = 0;  /* RET? */  // slot 629 [saf]
    virtual IOReturn setWCL_LIMITED_AGGREGATION(apple80211_limited_aggregation_config*) = 0;  /* RET? */  // slot 630 [saf]
    virtual IOReturn setWCL_BCN_MUTE_CONFIG(apple80211_bcn_mute_config*) = 0;  /* RET? */  // slot 631 [saf]
    virtual IOReturn setEAP_FILTER_CONFIG(apple80211_eap_filter_config*) = 0;  /* RET? */  // slot 632 [saf]
    virtual IOReturn setWOW_LOW_POWER_MODE(apple80211_wow_low_power_mode*) = 0;  /* RET? */  // slot 633 [saf]
    virtual IOReturn setDUAL_POWER_MODE(apple80211_dual_power_mode_params*) = 0;  /* RET? */  // slot 634 [saf]
    virtual IOReturn setWCL_UPDATE_FAST_LANE(apple80211_fastlane*) = 0;  /* RET? */  // slot 635 [saf]
    virtual IOReturn setWCL_ASSOCIATED_SLEEP(apple80211_associated_sleep_config*) = 0;  /* RET? */  // slot 636 [saf]
    virtual IOReturn setCONGESTION_CTRL_IND(apple80211_congestion_control_indication*) = 0;  /* RET? */  // slot 637 [saf]
    virtual IOReturn setSTAND_ALONE_MODE_STATE(apple80211_standalone_state*) = 0;  /* RET? */  // slot 638 [saf]
    virtual IOReturn setIPV6_PARAMS(apple80211_ipv6_params*) = 0;  /* RET? */  // slot 639 [saf]
    virtual IOReturn setINFRA_ENUMERATED(apple80211_infra_enumerated*) = 0;  /* RET? */  // slot 640 [saf]
    virtual IOReturn setLMTPC_CONFIG(apple80211_lmtpc_config*) = 0;  /* RET? */  // slot 641 [saf]
    virtual IOReturn setTRAFFIC_ENG_PARAMS(apple80211_traffic_eng_params*) = 0;  /* RET? */  // slot 642 [saf]
    virtual IOReturn setLE_SCAN_PARAM(apple80211_le_scan_params*) = 0;  /* RET? */  // slot 643 [saf]
    virtual IOReturn setTIMESYNC_GPIO(apple80211_timesync_gpio*) = 0;  /* RET? */  // slot 644 [saf]
    virtual IOReturn setHOST_CLOCK_INFO(apple80211_host_clock_info*) = 0;  /* RET? */  // slot 645 [saf]
    virtual IOReturn setFW_CLOCK_SOURCE(apple80211_fw_clock_source*) = 0;  /* RET? */  // slot 646 [saf]
    virtual IOReturn setTIMESYNC_TX_POLICY(apple80211_timesync_tx_policy*) = 0;  /* RET? */  // slot 647 [saf]
    virtual IOReturn setTIMESYNC_RX_POLICY(apple80211_timesync_rx_policy*) = 0;  /* RET? */  // slot 648 [saf]
    virtual IOReturn setTIMESTAMPING_EN(apple80211_timestamping_en*) = 0;  /* RET? */  // slot 649 [saf]
    virtual IOReturn setWCL_SOI_CONFIG(appl80211_sleep_on_inactivity_config*) = 0;  /* RET? */  // slot 650 [saf]
    virtual IOReturn setMWS_TIME_SHARING_WIFI_ENH(apple80211_mws_time_sharing*) = 0;  /* RET? */  // slot 651 [saf]
    virtual IOReturn setMWS_WIFI_TYPE_7_BITMAP_WIFI_ENH(apple80211_mws_wifi_channel_bitmap*) = 0;  /* RET? */  // slot 652 [saf]
    virtual IOReturn setMWS_COEX_BITMAP_WIFI_ENH(apple80211_mws_wifi_channel_bitmap*) = 0;  /* RET? */  // slot 653 [saf]
    virtual IOReturn setMWS_DISABLE_OCL_BITMAP_WIFI_ENH(apple80211_mws_wifi_channel_bitmap*) = 0;  /* RET? */  // slot 654 [saf]
    virtual IOReturn setMWS_RFEM_CONFIG_WIFI_ENH(apple80211_mws_rfem_config*) = 0;  /* RET? */  // slot 655 [saf]
    virtual IOReturn setMWS_ASSOC_PROTECTION_BITMAP_WIFI_ENH(apple80211_mws_wifi_channel_bitmap*) = 0;  /* RET? */  // slot 656 [saf]
    virtual IOReturn setMWS_SCAN_FREQ_WIFI_ENH(apple80211_mws_scan_freq*) = 0;  /* RET? */  // slot 657 [saf]
    virtual IOReturn setMWS_SCAN_FREQ_MODE_WIFI_ENH(apple80211_mws_scan_freq_mode*) = 0;  /* RET? */  // slot 658 [saf]
    virtual IOReturn setMWS_CONDITION_ID_BITMAP_WIFI_ENH(apple80211_mws_condition_id_config*) = 0;  /* RET? */  // slot 659 [saf]
    virtual IOReturn setMWS_ANTENNA_SELECTION_WIFI_ENH(apple80211_mws_antenna_selection*) = 0;  /* RET? */  // slot 660 [saf]
    virtual IOReturn setNDD_REQ(apple80211_ndd_data*) = 0;  /* RET? */  // slot 661 [saf]
    virtual IOReturn setDBRG_ENTROPY(apple80211_drbg_entropy*) = 0;  /* RET? */  // slot 662 [saf]
    virtual IOReturn setSDB_ENABLE(apple80211_sdb_enable*) = 0;  /* RET? */  // slot 663 [saf]
    virtual IOReturn setBTCOEX_EXT_PROFILE(apple80211_btcoex_ext_profile*) = 0;  /* RET? */  // slot 664 [saf]
    virtual IOReturn setDEVICE_ORIENTATION(apple80211_device_orientation*) = 0;  /* RET? */  // slot 665 [saf]
    virtual IOReturn setACCESSORY_STATE(apple80211_device_accessory_info*) = 0;  /* RET? */  // slot 666 [saf]
    virtual IOReturn setOS_ELIGIBILITY(apple80211_os_eligibility*) = 0;  /* RET? */  // slot 667 [saf]
    virtual IOReturn setTX_MODE_CONFIG(apple80211_tx_mode_config*) = 0;  /* RET? */  // slot 668 [saf]
    virtual IOReturn setMITIGATE_INTERFERENCE(apple80211_mitigate_interference*) = 0;  /* RET? */  // slot 669 [saf]
};
static_assert(sizeof(IO80211InfraProtocol) == 304, "IO80211InfraProtocol boyutu macOS 26 ile uyusmuyor");

class IO80211Controller : public IOEthernetController {
    OSDeclareAbstractStructors(IO80211Controller)
public:
    virtual void free();  // slot 20 [override]
    virtual bool init(OSDictionary*);  // slot 75 [override]
    virtual IOReturn configureReport(IOReportChannelList*, unsigned int, void*, void*);  // slot 135 [override]
    virtual IOReturn updateReport(IOReportChannelList*, unsigned int, void*, void*);  // slot 136 [override]
    virtual bool start(IOService*);  // slot 186 [override]
    virtual void stop(IOService*);  // slot 187 [override]
    virtual IOWorkLoop* getWorkLoop() const;  // slot 211 [override]
    virtual const char* stringFromReturn(int);  // slot 242 [override]
    virtual int errnoFromReturn(int);  // slot 243 [override]
    virtual UInt32 getFeatures() const;  // slot 270 [override]
    virtual const OSString * newVendorString() const;  // slot 271 [override]
    virtual const OSString * newModelString() const;  // slot 272 [override]
    virtual bool createWorkLoop();  // slot 308 [override]
    virtual IOReturn getHardwareAddress(IOEthernetAddress*);  // slot 358 [override]
    virtual IOReturn setMulticastMode(bool);  // slot 360 [override]
    virtual IOReturn setPromiscuousMode(bool);  // slot 362 [override]
    virtual bool isCommandProhibited(int) = 0;  // slot 396 [saf]
    virtual bool createWorkQueue();  // slot 397
    virtual IOReturn debugStateInit();  /* RET? */  // slot 398
    virtual IO80211WorkQueue * getWorkQueue() const;  // slot 399
    virtual void requestPacketTx(void*, unsigned int);  // slot 400
    virtual IOCommandGate * getIO80211CommandGate() const;  // slot 401
    virtual IO80211SkywalkInterface* getPrimarySkywalkInterface();  // slot 402
    virtual int bpfOutputPacket(OSObject*, unsigned int, __mbuf*);  // slot 403
    virtual SInt32 monitorModeSetEnabled(bool, unsigned int);  // slot 404
    virtual SInt32 handleCardSpecific(IO80211SkywalkInterface*, unsigned long, void*, bool) = 0;  // slot 405 [saf]
    virtual UInt32 hardwareOutputQueueDepth();  // slot 406
    virtual SInt32 performCountryCodeOperation(IO80211CountryCodeOp);  // slot 407
    virtual void dataLinkLayerAttachComplete();  // slot 408
    virtual SInt32 enableFeature(IO80211FeatureCode, void*);  // slot 409
    virtual IOReturn getDRIVER_VERSION(IO80211SkywalkInterface*, apple80211_version_data*) = 0;  // slot 410 [saf]
    virtual IOReturn getHARDWARE_VERSION(IO80211SkywalkInterface*, apple80211_version_data*) = 0;  // slot 411 [saf]
    virtual IOReturn getCARD_CAPABILITIES(IO80211SkywalkInterface*, apple80211_capability_data*) = 0;  // slot 412 [saf]
    virtual IOReturn getPOWER(IO80211SkywalkInterface*, apple80211_power_data*) = 0;  // slot 413 [saf]
    virtual IOReturn setPOWER(IO80211SkywalkInterface*, apple80211_power_data*) = 0;  // slot 414 [saf]
    virtual IOReturn getCOUNTRY_CODE(IO80211SkywalkInterface*, apple80211_country_code_data*) = 0;  // slot 415 [saf]
    virtual IOReturn setCOUNTRY_CODE(IO80211SkywalkInterface*, apple80211_country_code_data*) = 0;  // slot 416 [saf]
    virtual IOReturn setGET_DEBUG_INFO(IO80211SkywalkInterface*, apple80211_debug_command*) = 0;  // slot 417 [saf]
    virtual IOReturn getPLATFORM_CONFIG(IO80211SkywalkInterface*, apple80211_platform_config*);  /* RET? */  // slot 418
    virtual SInt32 enableVirtualInterface(IO80211VirtualInterface*);  // slot 419
    virtual SInt32 disableVirtualInterface(IO80211VirtualInterface*);  // slot 420
    virtual bool requiresExplicitMBufRelease();  // slot 421
    virtual bool flowIdSupported();  // slot 422
    virtual IO80211FlowQueueLegacy* requestFlowQueue(FlowIdMetadata const*);  // slot 423
    virtual void releaseFlowQueue(IO80211FlowQueue*);  // slot 424
    virtual bool getLogPipes(CCPipe**, CCPipe**, CCPipe**);  // slot 425
    virtual void * getLogger() const = 0;  // slot 426 [saf]
    virtual void enableFeatureForLoggingFlags(unsigned long long);  // slot 427
    virtual IOReturn requestQueueSizeAndTimeout(unsigned short*, unsigned short*);  // slot 428
    virtual IOReturn enablePacketTimestamping();  // slot 429
    virtual IOReturn disablePacketTimestamping();  // slot 430
    virtual UInt getPacketTSCounter();  // slot 431
    virtual void * getDriverTextLog();  // slot 432
    virtual UInt32 selfDiagnosticsReport(int, char const*, unsigned int);  // slot 433
    virtual void * getFaultReporterFromDriver() = 0;  // slot 434 [saf]
    virtual IOReturn allocIO80211RecursiveLock();  /* RET? */  // slot 435
    virtual UInt32 getDataQueueDepth(OSObject*);  // slot 436
    virtual bool wasDynSARInFailSafeMode();  // slot 437
    virtual void updateAdvisoryScoresIfNeed();  // slot 438
    virtual UInt64 getAVCAdvisoryInfo(IO80211InterfaceAVCAdvisory*);  // slot 439
    virtual IOReturn getActionFramePoolCapacity();  /* RET? */  // slot 440
    virtual IOReturn getPostOffice();  /* RET? */  // slot 441
    virtual IOReturn CreatePostOffice();  /* RET? */  // slot 442
    virtual bool attachInterface(OSObject*, IOService*);  // slot 443
    virtual void detachInterface(OSObject*, bool);  // slot 444
    virtual IO80211VirtualInterface* createVirtualInterface(ether_addr*, unsigned int);  // slot 445
    virtual bool attachVirtualInterface(IO80211VirtualInterface**, ether_addr*, unsigned int, bool);  // slot 446
    virtual bool detachVirtualInterface(IO80211VirtualInterface*, bool);  // slot 447
    virtual IOReturn _RESERVEDIO80211Controller0();  /* RET? */  // slot 448
    virtual IOReturn _RESERVEDIO80211Controller1();  /* RET? */  // slot 449
    virtual IOReturn _RESERVEDIO80211Controller2();  /* RET? */  // slot 450
    virtual IOReturn _RESERVEDIO80211Controller3();  /* RET? */  // slot 451
    virtual IOReturn _RESERVEDIO80211Controller4();  /* RET? */  // slot 452
    virtual IOReturn _RESERVEDIO80211Controller5();  /* RET? */  // slot 453
    virtual IOReturn _RESERVEDIO80211Controller6();  /* RET? */  // slot 454
    virtual IOReturn _RESERVEDIO80211Controller7();  /* RET? */  // slot 455
    virtual IOReturn _RESERVEDIO80211Controller8();  /* RET? */  // slot 456
    virtual IOReturn _RESERVEDIO80211Controller9();  /* RET? */  // slot 457
    virtual IOReturn _RESERVEDIO80211Controller10();  /* RET? */  // slot 458
    virtual IOReturn _RESERVEDIO80211Controller11();  /* RET? */  // slot 459
    virtual IOReturn _RESERVEDIO80211Controller12();  /* RET? */  // slot 460
    virtual IOReturn _RESERVEDIO80211Controller13();  /* RET? */  // slot 461
    virtual IOReturn _RESERVEDIO80211Controller14();  /* RET? */  // slot 462
    virtual IOReturn _RESERVEDIO80211Controller15();  /* RET? */  // slot 463
    virtual IOReturn setMulticastList(ether_addr const*, unsigned int);  // slot 464
private:
    uint8_t _r9t_pad[16];  // 26 classSize 296 - ust 280
};
static_assert(sizeof(IO80211Controller) == 296, "IO80211Controller boyutu macOS 26 ile uyusmuyor");

class IO80211WorkQueue : public IOWorkLoop {
    OSDeclareAbstractStructors(IO80211WorkQueue)
public:
    virtual IOThread getThread() const;  // slot 39 [override]
    virtual void enableAllInterrupts() const;  // slot 46 [override]
    virtual void disableAllInterrupts() const;  // slot 47 [override]
    virtual IOReturn runAction(int (*)(OSObject*, void*, void*, void*, void*), OSObject*, void*, void*, void*, void*);  // slot 54 [override]
    virtual int commandSleep(void*, unsigned long long);  // slot 65
    virtual void commandWakeup(void*);  // slot 66
    static IO80211WorkQueue *workQueue();  // sanal degil (YYavic)
private:
    uint8_t _r9t_pad[8];  // 26 classSize 80 - ust 72
};
static_assert(sizeof(IO80211WorkQueue) == 80, "IO80211WorkQueue boyutu macOS 26 ile uyusmuyor");
