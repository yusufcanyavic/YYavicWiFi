//
//  Copyright (C) 2026 Yusufcan Yavic
//  SPDX-License-Identifier: GPL-3.0-or-later
//
/* YYavic: OTOMATIK (gen_stubs.py) - 15.8.1 saf slot kaliplari, bildirimler */
    virtual IOReturn getPOWERSAVE(apple80211_powersave_data*) override;  // slot 468
    virtual IOReturn getTXPOWER(apple80211_txpower_data*) override;  // slot 469
    virtual IOReturn getGUARD_INTERVAL(apple80211_guard_interval_data*) override;  // slot 474
    virtual IOReturn getPOWER_DEBUG_INFO(apple80211_power_debug_info*) override;  // slot 476
    virtual IOReturn getHT_CAPABILITY(apple80211_ht_capability*) override;  // slot 477
    virtual IOReturn getVHT_CAPABILITY(apple80211_vht_capability*) override;  // slot 480
    virtual IOReturn getCHIP_COUNTER_STATS(apple80211_chip_stats*) override;  // slot 482
    virtual IOReturn getDBG_GUARD_TIME_PARAMS(apple80211_dbg_guard_time_params*) override;  // slot 483
    virtual IOReturn getLEAKY_AP_STATS_MODE(apple80211_leaky_ap_setting*) override;  // slot 484
    virtual IOReturn getCOUNTRY_CHANNELS(apple80211_country_channel_data*) override;  // slot 485
    virtual IOReturn getPRIVATE_MAC(apple80211_private_mac_data*) override;  // slot 486
    virtual IOReturn getRANGING_ENABLE(apple80211_ranging_enable_request_t*) override;  // slot 487
    virtual IOReturn getRANGING_START(apple80211_ranging_start_request_t*) override;  // slot 488
    virtual IOReturn getAWDL_RSDB_CAPS(apple80211_rsdb_capability*) override;  // slot 489
    virtual IOReturn getTKO_PARAMS(apple80211_tko_params*) override;  // slot 490
    virtual IOReturn getTKO_DUMP(apple80211_tko_dump*) override;  // slot 491
    virtual IOReturn getBTCOEX_PROFILE(apple80211_btcoex_profile*) override;  // slot 493
    virtual IOReturn getBTCOEX_PROFILE_ACTIVE(apple80211_btcoex_profile_active_data*) override;  // slot 494
    virtual IOReturn getTRAP_INFO(apple80211_trap_info_data*) override;  // slot 495
    virtual IOReturn getTHERMAL_INDEX(apple80211_thermal_index_t*) override;  // slot 496
    virtual IOReturn getBTCOEX_2G_CHAIN_DISABLE(apple80211_btcoex_2g_chain_disable*) override;  // slot 498
    virtual IOReturn getPOWER_BUDGET(apple80211_power_budget_t*) override;  // slot 499
    virtual IOReturn getOFFLOAD_TCPKA_ENABLE(apple80211_offload_tcpka_enable_t*) override;  // slot 500
    virtual IOReturn getRANGING_CAPS(apple80211_ranging_capabilities_t*) override;  // slot 501
    virtual IOReturn getLQM_CONFIG(apple80211_lqm_config_t*) override;  // slot 502
    virtual IOReturn getTRAP_CRASHTRACER_MINI_DUMP(apple80211_trap_mini_dump_data*) override;  // slot 503
    virtual IOReturn getBEACON_INFO(apple80211_beacon_info_t*) override;  // slot 504
    virtual IOReturn getCHIP_POWER_RANGE(apple80211_chip_power_limit*) override;  // slot 505
    virtual IOReturn getHW_ADDR(apple80211_hw_mac_address*) override;  // slot 507
    virtual IOReturn getCHIP_DIAGS(appl80211_chip_diags_data*) override;  // slot 508
    virtual IOReturn getHP2P_CTRL(apple80211_hp2p_ctrl*) override;  // slot 509
    virtual IOReturn getBSS_BLACKLIST(bss_blacklist*) override;  // slot 510 RET?
    virtual IOReturn getTXRX_CHAIN_INFO(apple80211_txrx_chain_info*) override;  // slot 511
    virtual IOReturn getMIMO_STATUS(apple80211_mimo_status*) override;  // slot 512
    virtual IOReturn getCUR_PMK(apple80211_pmk*) override;  // slot 513
    virtual IOReturn getDYNSAR_DETAIL(apple80211_dynsar_detail*) override;  // slot 514
    virtual IOReturn getLQM_SUMMARY(apple80211_lqm_summary*) override;  // slot 516
    virtual IOReturn getSLOW_WIFI_FEATURE_ENABLED(apple80211_slow_wifi_feature_enabled*) override;  // slot 517
    virtual IOReturn getTIMESYNC_INFO(apple80211_timesync_info*) override;  // slot 518
    virtual IOReturn getSENSING_DATA(apple80211_sensing_data_t*) override;  // slot 519
    virtual IOReturn getWCL_FW_HOT_CHANNELS(apple80211_fw_hot_channels*) override;  // slot 520
    virtual IOReturn getWCL_GET_TX_BLANKING_STATUS(unsigned int*) override;  // slot 524
    virtual IOReturn getHE_COUNTERS(apple80211_he_counters_ctl*) override;  // slot 525 RET?
    virtual IOReturn getWCL_CHANNELS_INFO(apple80211ChannelInfo*) override;  // slot 526
    virtual IOReturn getRSN_XE(apple80211_rsn_xe_data*) override;  // slot 527
    virtual IOReturn getSIB_COEX_STATUS(apple80211_sib_coex_status*) override;  // slot 528
    virtual IOReturn getWIFI_BT_5G_POLICY(apple80211_wifi_bt_5g_policy_t*) override;  // slot 529 RET?
    virtual IOReturn getWCL_LOW_LATENCY_INFO_STATS(apple80211_wcl_low_latency_stats*) override;  // slot 531
    virtual IOReturn getWCL_BGSCAN_CACHE_RESULT(apple80211_bgscan_cached_network_data_list*) override;  // slot 532
    virtual IOReturn getWCL_WNM_OFFLOAD(apple80211_wcl_wnm_offload_t*) override;  // slot 533 RET?
    virtual IOReturn getWIFI_NOISE_PER_ANT(apple80211_noise_per_ant_t*) override;  // slot 534
    virtual IOReturn getFW_CLOCK_INFO(apple80211_fw_clock_info*) override;  // slot 535 RET?
    virtual IOReturn getTIMESYNC_STATS(apple80211_timesync_stats*) override;  // slot 536 RET?
    virtual IOReturn getSYSTEM_SLEEP_CONFIG(apple80211_system_sleep_config*) override;  // slot 537 RET?
    virtual IOReturn setCIPHER_KEY(apple80211_key*) override;  // slot 538
    virtual IOReturn setCHANNEL(apple80211_channel_data*) override;  // slot 539
    virtual IOReturn setPOWERSAVE(apple80211_powersave_data*) override;  // slot 540
    virtual IOReturn setTXPOWER(apple80211_txpower_data*) override;  // slot 541
    virtual IOReturn setRATE(apple80211_rate_data*) override;  // slot 542
    virtual IOReturn setIBSS_MODE(apple80211_network_data*) override;  // slot 543
    virtual IOReturn setAP_MODE(apple80211_apmode_data*) override;  // slot 544
    virtual IOReturn setIE(apple80211_ie_data*) override;  // slot 545
    virtual IOReturn setWOW_TEST(apple80211_wow_test_data*) override;  // slot 546
    virtual IOReturn setCLEAR_PMKSA_CACHE(void*) override;  // slot 547 RET?
    virtual IOReturn setVIRTUAL_IF_CREATE(apple80211_virt_if_create_data*) override;  // slot 548
    virtual IOReturn setHT_CAPABILITY(apple80211_ht_capability*) override;  // slot 549
    virtual IOReturn setOFFLOAD_ARP(apple80211_offload_arp_data*) override;  // slot 550
    virtual IOReturn setOFFLOAD_NDP(apple80211_offload_ndp_data*) override;  // slot 551
    virtual IOReturn setGAS_REQ(apple80211_gas_query_t*) override;  // slot 552
    virtual IOReturn setVHT_CAPABILITY(apple80211_vht_capability*) override;  // slot 553
    virtual IOReturn setROAM_PROFILE(apple80211_roam_profile_all_bands*) override;  // slot 554
    virtual IOReturn setDBG_GUARD_TIME_PARAMS(apple80211_dbg_guard_time_params*) override;  // slot 555
    virtual IOReturn setLEAKY_AP_STATS_MODE(apple80211_leaky_ap_setting*) override;  // slot 556
    virtual IOReturn setPRIVATE_MAC(apple80211_private_mac_data*) override;  // slot 557
    virtual IOReturn setRESET_CHIP(apple80211_reset_command*) override;  // slot 558
    virtual IOReturn setCRASH(apple80211_crash_command*) override;  // slot 559
    virtual IOReturn setRANGING_ENABLE(apple80211_ranging_enable_request_t*) override;  // slot 560
    virtual IOReturn setRANGING_START(apple80211_ranging_start_request_t*) override;  // slot 561
    virtual IOReturn setRANGING_AUTHENTICATE(apple80211_ranging_authenticate_request_t*) override;  // slot 562
    virtual IOReturn setTKO_PARAMS(apple80211_tko_params*) override;  // slot 563
    virtual IOReturn setBTCOEX_PROFILE(apple80211_btcoex_profile*) override;  // slot 564
    virtual IOReturn setBTCOEX_PROFILE_ACTIVE(apple80211_btcoex_profile_active_data*) override;  // slot 565
    virtual IOReturn setTHERMAL_INDEX(apple80211_thermal_index_t*) override;  // slot 566
    virtual IOReturn setBTCOEX_2G_CHAIN_DISABLE(apple80211_btcoex_2g_chain_disable*) override;  // slot 567
    virtual IOReturn setPOWER_BUDGET(apple80211_power_budget_t*) override;  // slot 568
    virtual IOReturn setOFFLOAD_TCPKA_ENABLE(apple80211_offload_tcpka_enable_t*) override;  // slot 569
    virtual IOReturn setLQM_CONFIG(apple80211_lqm_config_t*) override;  // slot 570
    virtual IOReturn setDYNAMIC_RSSI_WINDOW_CONFIG(apple80211_dynamic_rssi_window_config*) override;  // slot 571 RET?
    virtual IOReturn setUSB_HOST_NOTIFICATION(apple80211_usb_host_notification_data*) override;  // slot 572
    virtual IOReturn setHP2P_CTRL(apple80211_hp2p_ctrl*) override;  // slot 573
    virtual IOReturn setBSS_BLACKLIST(bss_blacklist*) override;  // slot 574 RET?
    virtual IOReturn setSET_PROPERTY(apple80211_set_property_unserialized_data*) override;  // slot 575
    virtual IOReturn setROAM_CACHE_UPDATE(apple80211_roam_cache_data*) override;  // slot 576
    virtual IOReturn setPM_MODE(apple80211_pm_mode*) override;  // slot 577
    virtual IOReturn setSET_WIFI_ASSERTION_STATE(apple80211_wifi_assertion_data*) override;  // slot 578
    virtual IOReturn setREALTIME_QOS_MSCS(apple80211_state_data*) override;  // slot 579
    virtual IOReturn setSENSING_ENABLE(apple80211_sensing_enable_t*) override;  // slot 580
    virtual IOReturn setSENSING_DISABLE(apple80211_sensing_disable_t*) override;  // slot 581
    virtual IOReturn set6G_MODE(apple80211_6G_mode*) override;  // slot 582
    virtual IOReturn setWCL_REASSOC(apple80211_reassoc*) override;  // slot 584
    virtual IOReturn setWCL_SET_ROAM_LOCK(apple80211_set_roam_lock*) override;  // slot 585
    virtual IOReturn setWCL_LEGACY_ROAM_PROFILE_CONFIG(apple80211_legacy_roam_profile_config*) override;  // slot 586 RET?
    virtual IOReturn setWCL_ROAM_PROFILE_CONFIG(apple80211_roam_profile_config*) override;  // slot 587
    virtual IOReturn setWCL_ROAM_USER_CACHE(apple80211_user_roam_cache*) override;  // slot 588
    virtual IOReturn setWCL_SCAN_ABORT(void*) override;  // slot 589
    virtual IOReturn setWCL_REAL_TIME_MODE(apple80211_wcl_real_time_mode*) override;  // slot 590
    virtual IOReturn setWCL_ARP_MODE(apple80211_wcl_arp_mode*) override;  // slot 591 RET?
    virtual IOReturn setWCL_TRIGGER_CC(triggerCC*) override;  // slot 593
    virtual IOReturn setWCL_QOS_PARAMS(apple80211_wcl_qos_params*) override;  // slot 596 RET?
    virtual IOReturn setWCL_LINK_UP_DONE(void*) override;  // slot 597
    virtual IOReturn setWCL_SET_SCAN_HOME_AWAY_TIME(scanHomeAndAwayTime*) override;  // slot 598
    virtual IOReturn setVOICE_IND_STATE(apple80211_voice_ind_state*) override;  // slot 599 RET?
    virtual IOReturn setWCL_LINK_STATE_UPDATE(apple80211_wcl_update_link_state*) override;  // slot 600
    virtual IOReturn setRSN_XE(apple80211_rsn_xe_data*) override;  // slot 601
    virtual IOReturn setWIFI_BT_5G_POLICY(apple80211_wifi_bt_5g_policy_t*) override;  // slot 602 RET?
    virtual IOReturn setWCL_ULOFDMA_STATE(apple80211_wcl_ulofdma_state*) override;  // slot 603
    virtual IOReturn setWCL_ACTION_FRAME(apple80211_wcl_action_frame*) override;  // slot 604
    virtual IOReturn setGAS_ABORT(void*) override;  // slot 605
    virtual IOReturn setOS_FEATURE_FLAGS(apple80211_feature_flags*) override;  // slot 606
    virtual IOReturn setDHCP_RENEWAL_DATA(apple80211_dhcp_renewal_data*) override;  // slot 607
    virtual IOReturn setBATTERY_POWERSAVE_CONFIG(apple80211_battery_ps_config*) override;  // slot 608
    virtual IOReturn setMIMO_CONFIG(apple80211_mimo_config*) override;  // slot 609
    virtual IOReturn setWCL_CONFIG_BG_MOTIONPROFILE(apple80211_bg_motion_profile*) override;  // slot 610
    virtual IOReturn setWCL_CONFIG_BG_NETWORK(apple80211_bg_network*) override;  // slot 611
    virtual IOReturn setWCL_CONFIG_BGSCAN(apple80211_bg_scan*) override;  // slot 612
    virtual IOReturn setWCL_CONFIG_BG_PARAMS(apple80211_bg_params*) override;  // slot 613
    virtual IOReturn setPOWER_PROFILE(apple80211_power_profile*) override;  // slot 614 RET?
    virtual IOReturn setHEARTBEAT(void*) override;  // slot 615 RET?
    virtual IOReturn setINTERFACE_SETTING(apple80211_interface_setting*) override;  // slot 616 RET?
    virtual IOReturn setBYPASS_TX_POWER_CAP(apple80211_bypass_tx_power_cap*) override;  // slot 617 RET?
    virtual IOReturn setFACETIME_WIFICALLING_PARAMS(apple80211_facetime_wificalling_params*) override;  // slot 618 RET?
    virtual IOReturn setIPV4_PARAMS(apple80211_ipv4_params*) override;  // slot 619 RET?
    virtual IOReturn setWCL_WNM_OPS(apple80211_wcl_wnm_config_t*) override;  // slot 620 RET?
    virtual IOReturn setWCL_WNM_OFFLOAD(apple80211_wcl_wnm_offload_t*) override;  // slot 621 RET?
    virtual IOReturn setWCL_LIMITED_AGGREGATION(apple80211_limited_aggregation_config*) override;  // slot 622 RET?
    virtual IOReturn setWCL_BCN_MUTE_CONFIG(apple80211_bcn_mute_config*) override;  // slot 623 RET?
    virtual IOReturn setEAP_FILTER_CONFIG(apple80211_eap_filter_config*) override;  // slot 624 RET?
    virtual IOReturn setWOW_LOW_POWER_MODE(apple80211_wow_low_power_mode*) override;  // slot 625 RET?
    virtual IOReturn setDUAL_POWER_MODE(apple80211_dual_power_mode_params*) override;  // slot 626 RET?
    virtual IOReturn setWCL_UPDATE_FAST_LANE(apple80211_fastlane*) override;  // slot 627 RET?
    virtual IOReturn setWCL_ASSOCIATED_SLEEP(apple80211_associated_sleep_config*) override;  // slot 628 RET?
    virtual IOReturn setCONGESTION_CTRL_IND(apple80211_congestion_control_indication*) override;  // slot 629 RET?
    virtual IOReturn setSTAND_ALONE_MODE_STATE(apple80211_standalone_state*) override;  // slot 630 RET?
    virtual IOReturn setIPV6_PARAMS(apple80211_ipv6_params*) override;  // slot 631 RET?
    virtual IOReturn setINFRA_ENUMERATED(apple80211_infra_enumerated*) override;  // slot 632 RET?
    virtual IOReturn setLMTPC_CONFIG(apple80211_lmtpc_config*) override;  // slot 633 RET?
    virtual IOReturn setTRAFFIC_ENG_PARAMS(apple80211_traffic_eng_params*) override;  // slot 634 RET?
    virtual IOReturn setLE_SCAN_PARAM(apple80211_le_scan_params*) override;  // slot 635 RET?
    virtual IOReturn setTIMESYNC_GPIO(apple80211_timesync_gpio*) override;  // slot 636 RET?
    virtual IOReturn setHOST_CLOCK_INFO(apple80211_host_clock_info*) override;  // slot 637 RET?
    virtual IOReturn setFW_CLOCK_SOURCE(apple80211_fw_clock_source*) override;  // slot 638 RET?
    virtual IOReturn setTIMESYNC_TX_POLICY(apple80211_timesync_tx_policy*) override;  // slot 639 RET?
    virtual IOReturn setTIMESYNC_RX_POLICY(apple80211_timesync_rx_policy*) override;  // slot 640 RET?
    virtual IOReturn setTIMESTAMPING_EN(apple80211_timestamping_en*) override;  // slot 641 RET?
    virtual IOReturn setWCL_SOI_CONFIG(appl80211_sleep_on_inactivity_config*) override;  // slot 642 RET?
    virtual IOReturn setMWS_TIME_SHARING_WIFI_ENH(apple80211_mws_time_sharing*) override;  // slot 643 RET?
    virtual IOReturn setMWS_WIFI_TYPE_7_BITMAP_WIFI_ENH(apple80211_mws_wifi_channel_bitmap*) override;  // slot 644 RET?
    virtual IOReturn setMWS_COEX_BITMAP_WIFI_ENH(apple80211_mws_wifi_channel_bitmap*) override;  // slot 645 RET?
    virtual IOReturn setMWS_DISABLE_OCL_BITMAP_WIFI_ENH(apple80211_mws_wifi_channel_bitmap*) override;  // slot 646 RET?
    virtual IOReturn setMWS_RFEM_CONFIG_WIFI_ENH(apple80211_mws_rfem_config*) override;  // slot 647 RET?
    virtual IOReturn setMWS_ASSOC_PROTECTION_BITMAP_WIFI_ENH(apple80211_mws_wifi_channel_bitmap*) override;  // slot 648 RET?
    virtual IOReturn setMWS_SCAN_FREQ_WIFI_ENH(apple80211_mws_scan_freq*) override;  // slot 649 RET?
    virtual IOReturn setMWS_SCAN_FREQ_MODE_WIFI_ENH(apple80211_mws_scan_freq_mode*) override;  // slot 650 RET?
    virtual IOReturn setMWS_CONDITION_ID_BITMAP_WIFI_ENH(apple80211_mws_condition_id_config*) override;  // slot 651 RET?
    virtual IOReturn setMWS_ANTENNA_SELECTION_WIFI_ENH(apple80211_mws_antenna_selection*) override;  // slot 652 RET?
    virtual IOReturn setNDD_REQ(apple80211_ndd_data*) override;  // slot 653 RET?
    virtual IOReturn setDBRG_ENTROPY(apple80211_drbg_entropy*) override;  // slot 654 RET?
    virtual IOReturn setSDB_ENABLE(apple80211_sdb_enable*) override;  // slot 655 RET?
    virtual IOReturn setMWS_ACCESSORY_POWER_LIMIT_WIFI_ENH(apple80211_mws_accessory_power_limit*) override;  // slot 656 RET?
