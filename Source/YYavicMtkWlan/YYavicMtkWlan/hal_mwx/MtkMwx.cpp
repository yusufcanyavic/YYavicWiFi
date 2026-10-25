/*
 * Reworked by Yusufcan Yavic, 2026, for YYavicWiFi (native MT7922 Wi-Fi on macOS).
 * Starting point: the SimpleMtkWlan version of this file by laobamac.
 * Added and changed by Yusufcan Yavic: Wi-Fi 6 (HE) and 160 MHz firmware setup, RX A-MSDU reassembly, BlockAck 256, larger DMA rings and TXWI pool, BSS RLM fix, HE rate decoding, AMD RZ616 (14c3:0616) support, per-country channel sets (yyavic-cc), diagnostics, stability fixes.
 * See CHANGELOG.md. The original notices below remain in force; these
 * modifications are distributed under the same license as this file.
 */
//
//  MtkMwx.cpp
//  YYavicWiFi
//
//  Originally written by laobamac (王孝慈) on 2026/5/5 for SimpleMtkWlan.
//  Copyright © 2026 laobamac (王孝慈). All rights reserved.
//

/*	$OpenBSD: if_mwx.c,v 1.9 2026/05/09 12:13:15 claudio Exp $ */
/*
 * Copyright (c) 2022 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2021 MediaTek Inc.
 * Copyright (c) 2021 Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
 * Copyright (c) 2017 Stefan Sperling <stsp@openbsd.org>
 * Copyright (c) 2016 Felix Fietkau <nbd@nbd.name>
 * Copyright (c) 2007-2010 Damien Bergamini <damien.bergamini@free.fr>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "MtkMwx.hpp"

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/bitfield.h>
#include <sys/kpi_mbuf.h>
#include <sys/_if_ether.h>
#include <sys/_if_media.h>
#include <sys/_malloc.h>
#include <sys/_mbuf.h>
#include <sys/_task.h>
#include <net/ethernet.h>
#include <net80211/ieee80211_proto.h>
#include <net80211/ieee80211_radiotap.h>
#include <FwData.h>

#include "if_mwxreg.h"

OSDefineMetaClassAndStructors(MtkMwx, MtkHalService)

#ifndef NBPFILTER
#define NBPFILTER 0
#endif

#define PCI_VENDOR_MEDIATEK            0x14c3
#define PCI_PRODUCT_MEDIATEK_MT7921    0x7961
#define PCI_PRODUCT_MEDIATEK_MT7921K   0x0608
#define PCI_PRODUCT_MEDIATEK_MT7922    0x7922
/* AMD RZ616: an MT7922 (Linux mt7921/pci.c loads the MT7922 firmware for it) */
#define PCI_PRODUCT_MEDIATEK_RZ616     0x0616

#define MWX_LOG(x...) smtw_trace("YYavicMtkWlan-mwx: " x)
#define MWX_DEV_LOG(sc, fmt, args...) \
	smtw_trace("YYavicMtkWlan-mwx: %s: " fmt, DEVNAME(sc), ##args)

#define ifnet _ifnet
#define mbuf __mbuf

#undef KASSERT
#define KASSERT(exp, ...) do { if (!(exp)) panic("mwx assertion failed: %s", #exp); } while (0)
#ifndef __unused
#define __unused __attribute__((unused))
#endif
#ifndef ISSET
#define ISSET(t, f) (((t) & (f)) != 0)
#endif
#ifndef IEEE80211_CHAN_40MHZ
#define IEEE80211_CHAN_40MHZ IEEE80211_CHAN_HT40
#endif
#ifndef IEEE80211_CHAN_6GHZ
#define IEEE80211_CHAN_6GHZ 0
#endif
#ifndef IEEE80211_IS_CHAN_6GHZ
#define IEEE80211_IS_CHAN_6GHZ(_c) false
#endif

#ifndef M_DEVBUF
#define M_DEVBUF 0
#endif
#ifndef M_NOWAIT
#define M_NOWAIT 0
#endif
#ifndef M_WAITOK
#define M_WAITOK 0
#endif
#ifndef M_ZERO
#define M_ZERO 0
#endif

static MtkMwx *gMwxHal;
/* R9T: MSI kesmesi firmware sonrasi servis edilmiyor -> workloop uzerinde 2 ms'lik yoklama zamanlayicisi (mwx_intr). */
extern volatile thread_t gMwxWlThread;
static IOTimerEventSource *gMwxPollTimer;
static struct mwx_softc *gMwxPollSc;
static volatile UInt32 gMwxPollEnable;
static volatile UInt32 gMwxIntrCalls;
static volatile UInt32 gMwxPollFires;
static volatile int gMwxTrapMode = 1;
/* R9T trace34: TX sayaclari (dbg 19/21) ve geri basinc (dbg 20) */
static uint32_t gTxDeq, gTxOk, gTxNoTxwi, gTxRingFull, gTxMapFail, gTxOactSet, gTxRestart, gTxFreeEv, gTxFreed;
static int32_t gTxwiUse, gTxwiPeak;
static volatile int gTxBackpressure = 1;
static volatile uint32_t gMwxPollUs = 20000;	/* YYavic msi2: MSI veri yolunu tasiyor, yoklama yalniz guvenlik agi (dbg 22) */
/* R9T trace29: RX istatistikleri (dbg op 11/12) */
static uint32_t gRxVht[2][10];	/* R9T trace39: [nss-1][mcs] */
static uint64_t gPollBusyAbs, gPollWinAbs0; static uint32_t gPollCalls;
static uint32_t gRxOfdm[16], gRxMode[16], gRxHtMcs[32], gRxSgi, gRxBw[4], gRxData, gRxRetry, gRxG3;   /* 0=sadece log, 1=EN bitlerini yeniden set, 2=mwx_dma_reset */
static volatile UInt32 gMwxHistN;
static uint32_t gMwxHist[16][3];   /* ms, intr, intr_sw */
extern "C" uint32_t mwx_poll_fires(void) { return gMwxPollFires; }
extern "C" volatile UInt32 gCompatIntrCount;	/* YYavic: compat.cpp MSI */
static uint32_t gYYRxCM, gYYRxCLM, gYYRxLastRxd1;	/* YYavic dbg 30 */
static uint32_t gYYRxFillFail, gYYRxFillRetry;	/* YYavic: RX ring refill failures / retries (dbg 29) */
/* YYavic: rate of the last HT/VHT frame received (link rate shown by macOS) */
static volatile uint8_t gYYRxMode, gYYRxMcs, gYYRxNss, gYYRxBw, gYYRxSgi;
uint8_t gYYNoHe;	/* YYavic: 1 = no 802.11ax (boot-arg -yyavicnohe, dbg 33) */
static uint8_t gYYRlmRx = 0xff, gYYRlmHtOp = 0xff;	/* dbg 36: BSS RLM overrides, 0xff = default */
/* YYavic RX A-MSDU (dbg 34): subframes, joined MPDUs, singles, bad, no-DA fallback, flushes by kind */
static uint32_t gYYAmsduSub, gYYAmsduMpdu, gYYAmsduSingle, gYYAmsduBad, gYYAmsduNoDa, gYYAmsduMaxN;
static uint32_t gYYAmsduFlushSeq, gYYAmsduFlushEnd, gYYAmsduFlushFull, gYYAmsduAllocFail;
#define	MWX_RXI_AMSDU_SHIFT	28	/* driver-private rxi_flags bits 28-29: RXD4 payload format */
#define	MWX_RXI_AMSDU_MASK	(3U << MWX_RXI_AMSDU_SHIFT)
static uint32_t gYYRxHe, gYYRxHeMcs[16];	/* YYavic dbg 33: HE RX frames */
/* YYavic perf (dbg 32): cumulative mach_absolute_time per stage on the radio work loop */
static uint64_t gYYPfIntr, gYYPfDeq, gYYPfFill, gYYPfProc, gYYPfIfIn, gYYPfStart, gYYPfTxDone;
static uint32_t gYYPfIntrN, gYYPfRxPkts, gYYPfRxCalls, gYYPfStartN;
extern "C" volatile int gCompatIntrIndex, gCompatIntrType;
static volatile UInt32 gMwxIntrBusy;
/* R9T: TX temizleme / halka sifirlama start ipligi ile workloop (kesme/yoklama) arasinda yaristi -> m_free double free paniği. */
static IOLock *gMwxQLock;
static inline IOLock *mwx_qlock(void)
{
	if (gMwxQLock == NULL) {
		IOLock *l = IOLockAlloc();
		if (l != NULL && !OSCompareAndSwapPtr(NULL, l, &gMwxQLock))
			IOLockFree(l);
	}
	return gMwxQLock;
}
static volatile UInt32 gMwxBodyCalls, gMwxCasFails, gMwxWaitPoll, gMwxWaitSleep;

static inline void *
mallocarray(size_t n, size_t size, int type, int flags)
{
	if (size != 0 && n > SIZE_MAX / size)
		return NULL;
	return malloc(n * size, type, flags);
}

static inline void
mwx_free_mem(void *addr)
{
	free(addr);
}

#define free(addr, type, size) mwx_free_mem(addr)

static inline int
loadfirmware(const char *name, u_char **buf, size_t *len)
{
	MWX_LOG("firmware lookup start name=%s\n", name);
	OSData *data = getFWDescByName(name);
	if (data == NULL) {
		MWX_LOG("firmware lookup failed name=%s error=%d\n", name, ENOENT);
		return ENOENT;
	}
	*len = data->getLength();
	*buf = (u_char *)malloc(*len, M_DEVBUF, M_WAITOK);
	if (*buf == NULL) {
		data->release();
		MWX_LOG("firmware copy allocation failed name=%s size=%zu error=%d\n",
		    name, *len, ENOMEM);
		return ENOMEM;
	}
	memcpy(*buf, data->getBytesNoCopy(), *len);
	data->release();
	MWX_LOG("firmware lookup ok name=%s size=%zu\n", name, *len);
	return 0;
}

static inline int
tsleep_nsec(void *ident, int priority, const char *wmesg, uint64_t timo)
{
	if (gMwxHal == NULL)
		return EWOULDBLOCK;
	return gMwxHal->mwxSleepNsec(ident, priority, wmesg, timo);
}

static inline bool
mwx_on_workloop(void)
{
	return gMwxHal != NULL && gMwxHal->mwxOnWorkLoop();
}

static inline void
config_mountroot(struct device *self, void (*fn)(struct device *))
{
	fn(self);
}

static inline void delay(int usec) {
    if (usec <= 0) return;
    if (usec < 1000) IODelay(usec);
    else IOSleep((usec + 999) / 1000);
}

static inline struct mbuf *
m_pullup(struct mbuf *m, size_t len)
{
	struct mbuf *n = NULL;
	size_t pktlen;
	u_int32_t chunks = 1;
	static uint32_t pullup_rebuild_log_count;

	if (m == NULL)
		return NULL;
	if (mbuf_len(m) >= len)
		return m;

	pktlen = mbuf_pkthdr_len(m);
	if (len > pktlen) {
		m_freem(m);
		return NULL;
	}

	if (pullup_rebuild_log_count < 32) {
		MWX_LOG("m_pullup rebuild len=%zu first_len=%zu pktlen=%zu\n",
		    len, mbuf_len(m), pktlen);
		pullup_rebuild_log_count++;
	}
	if (mbuf_allocpacket(M_DONTWAIT, pktlen, &chunks, &n) != 0) {
		m_freem(m);
		return NULL;
	}
	mbuf_setlen(n, pktlen);
	mbuf_pkthdr_setlen(n, pktlen);
	if (mbuf_copydata(m, 0, pktlen, mtod(n, void *)) != 0) {
		m_freem(n);
		m_freem(m);
		return NULL;
	}
	m_freem(m);
	return n;
}

static inline void
m_adj(struct mbuf *m, int len)
{
	mbuf_adj(m, len);
}

static inline struct mbuf *
m_prepend(struct mbuf *m, size_t len, int how)
{
	return mbuf_prepend(&m, len, how) == 0 ? m : NULL;
}

static inline size_t
m_leadingspace(struct mbuf *m)
{
	return mbuf_leadingspace(m);
}

static inline size_t
m_trailingspace(struct mbuf *m)
{
	return mbuf_trailingspace(m);
}

static inline struct mbuf *
m_gethdr(int how, int type)
{
	struct mbuf *m = NULL;
	mbuf_gethdr(how, type, &m);
	return m;
}

static inline struct mbuf *
MCLGETL(struct mbuf *m, int how, size_t len)
{
	u_int32_t chunks = 1;
	if (m == NULL) {
		if (mbuf_allocpacket(how, len, &chunks, &m) != 0)
			return NULL;
	} else if (mbuf_getcluster(how, mbuf_type(m), len, &m) != 0) {
		return NULL;
	}
	return m;
}

static const struct pci_matchid mwx_devices[] = {
	{ PCI_VENDOR_MEDIATEK, PCI_PRODUCT_MEDIATEK_MT7921 },
	{ PCI_VENDOR_MEDIATEK, PCI_PRODUCT_MEDIATEK_MT7921K },
	{ PCI_VENDOR_MEDIATEK, PCI_PRODUCT_MEDIATEK_MT7922 },
	{ PCI_VENDOR_MEDIATEK, PCI_PRODUCT_MEDIATEK_RZ616 },
};

/* Keep the OpenBSD DPRINTF path available, but do not enable it by default.
 * The PCIe interrupt and MCU paths can otherwise flood the kernel log. */
/* #define MWX_DEBUG	1 */

#define	MT7921_ROM_PATCH	"mwx-mt7961_patch_mcu_1_2_hdr"
#define	MT7921_FIRMWARE_WM	"mwx-mt7961_ram_code_1"
#define	MT7922_ROM_PATCH	"mwx-mt7922_patch_mcu_1_1_hdr"
#define	MT7922_FIRMWARE_WM	"mwx-mt7922_ram_code_1"

#if NBPFILTER > 0
struct mwx_rx_radiotap_header {
	struct ieee80211_radiotap_header	wr_ihdr;
	uint64_t				wr_tsft;
	uint8_t					wr_flags;
	uint8_t					wr_rate;
	uint16_t				wr_chan_freq;
	uint16_t				wr_chan_flags;
	int8_t					wr_dbm_antsignal;
	int8_t					wr_dbm_antnoise;
} __packed;

#define	MWX_RX_RADIOTAP_PRESENT				\
	((1 << IEEE80211_RADIOTAP_TSFT) |		\
	(1 << IEEE80211_RADIOTAP_FLAGS) |		\
	(1 << IEEE80211_RADIOTAP_RATE) |		\
	(1 << IEEE80211_RADIOTAP_CHANNEL) |		\
	(1 << IEEE80211_RADIOTAP_DBM_ANTSIGNAL) |	\
	(1 << IEEE80211_RADIOTAP_DBM_ANTNOISE))

struct mwx_tx_radiotap_header {
	struct ieee80211_radiotap_header	wt_ihdr;
	uint8_t					wt_flags;
	uint8_t					wt_rate;
	uint16_t				wt_chan_freq;
	uint16_t				wt_chan_flags;
} __packed;

#define	MWX_TX_RADIOTAP_PRESENT			\
	((1 << IEEE80211_RADIOTAP_FLAGS) |	\
	(1 << IEEE80211_RADIOTAP_RATE) |	\
	(1 << IEEE80211_RADIOTAP_CHANNEL))

#endif

struct mwx_txwi {
	struct mt76_txwi		*mt_desc;
	struct mbuf			*mt_mbuf;
	bus_dmamap_t			mt_map;
	LIST_ENTRY(mwx_txwi)		mt_entry;
	u_int32_t			mt_addr;
	u_int				mt_idx;
	int				mt_busy;
};

struct mwx_txwi_desc {
	struct mt76_txwi		*mt_desc;
	struct mwx_txwi			*mt_data;

	u_int				mt_count;
	bus_dmamap_t			mt_map;
	bus_dma_segment_t		mt_seg;
	LIST_HEAD(, mwx_txwi)		mt_freelist;
};

struct mwx_queue_data {
	struct mbuf			*md_mbuf;
	struct mwx_txwi			*md_txwi;
	bus_dmamap_t			md_map;
};

struct mwx_queue {
	uint32_t			mq_regbase;
	u_int				mq_count;
	u_int				mq_prod;
	u_int				mq_cons;

	struct mwx_desc		*mq_desc;
	struct mwx_queue_data		*mq_data;

	bus_dmamap_t			mq_map;
	bus_dma_segment_t		mq_seg;
	int				mq_wakeme;
};

struct mwx_hw_capa {
	int8_t		has_2ghz;
	int8_t		has_5ghz;
	int8_t		has_6ghz;
	uint8_t		antenna_mask;
	uint8_t		num_streams;
};

struct mwx_node {
	struct ieee80211_node	ni;
	uint16_t		wcid;
	uint16_t		flags;
};
#define	MWX_NODE_FLAG_HAVE_PAIRWISE_KEY		0x1
#define	MWX_NODE_FLAG_HAVE_GROUP_KEY		0x2

struct mwx_vif {
	struct mwx_node		vif_mn;
	uint8_t			idx;
	uint8_t			omac_idx;
	uint8_t			band_idx;
	uint8_t			wmm_idx;
	uint8_t			scan_seq_num;
};

enum mwx_hw_type {
	MWX_HW_MT7921,
	MWX_HW_MT7922,
};

struct mwx_setkey_task_arg {
	struct ieee80211_node	*ni;
	struct ieee80211_key	*k;
};

struct mwx_softc {
	struct device		sc_dev;
	struct ieee80211com	sc_ic;

	enum mwx_hw_type	sc_hwtype;

	struct mwx_queue	sc_txq;
	struct mwx_queue	sc_txmcuq;
	struct mwx_queue	sc_txfwdlq;

	struct mwx_queue	sc_rxq;
	struct mwx_queue	sc_rxmcuq;
	struct mwx_queue	sc_rxfwdlq;

	struct mwx_txwi_desc	sc_txwi;

	bus_space_tag_t		sc_st;
	bus_space_handle_t	sc_memh;
	bus_size_t		sc_mems;
	bus_dma_tag_t		sc_dmat;
	pcitag_t		sc_tag;
	pci_chipset_tag_t	sc_pc;
	void			*sc_ih;

	int			(*sc_newstate)(struct ieee80211com *,
				    enum ieee80211_state, int);

	struct taskq		*sc_nswq;
	struct task		sc_newstate_task;
	struct task		sc_bgscan_done_task;
	struct task		sc_setkey_task;
	struct task		sc_scan_task;
	struct task		sc_reset_task;
	CTimeout		*sc_reset_to;
	u_int			sc_flags;
#define MWX_FLAG_SCANNING		0x01
#define MWX_FLAG_BGSCAN			0x02
	int			sc_coredump_cnt;
	int8_t			sc_resetting;
	bool sc_detaching, sc_net_attached;
	int8_t			sc_fw_loaded;

	enum ieee80211_state	sc_ns_state;
	int			sc_ns_arg;

	struct ieee80211_node_switch_bss_arg	*sc_bgscan_arg;
	size_t			sc_bgscan_arg_size;

	struct mwx_setkey_task_arg	sc_setkey_arg[8];
	int			sc_setkey_cur;
	int			sc_setkey_tail;

	/* R9T trace32: RX Block Ack istekleri (gorevde FW'ye bildirilir) */
	struct task		sc_ba_task;
	struct {
		struct ieee80211_node	*ni;
		uint8_t			tid;
		uint8_t			start;
		uint8_t			tx;	/* R9T trace37: 1 = originator (TX BA) */
	}			sc_ba_arg[16];
	int			sc_ba_cur;
	int			sc_ba_tail;

#if NBPFILTER > 0
	caddr_t			sc_drvbpf;
	union {
		struct mwx_rx_radiotap_header th;
		uint8_t pad[IEEE80211_RADIOTAP_HDRLEN];
	}			sc_rxtapu;
	int			sc_rxtap_len;
	union {
		struct mwx_tx_radiotap_header th;
		uint8_t pad[IEEE80211_RADIOTAP_HDRLEN];
	}			sc_txtapu;
	int			sc_txtap_len;
#define	sc_rxtap	sc_rxtapu.th
#define	sc_txtap	sc_txtapu.th
#endif

	struct mwx_vif		sc_vif;
	uint32_t		sc_wcid_mask;
	uint32_t		sc_intr_mask;

	/* mcu */
	uint32_t		sc_mcu_seq;
	struct {
		struct mbuf	*mcu_m;
		uint32_t	 mcu_cmd;
		uint32_t	 mcu_int;
	}			sc_mcu_wait[16];
	uint8_t			sc_scan_seq_num;

	/* phy / hw */
	struct mwx_hw_capa	sc_capa;
	uint8_t			sc_lladdr[ETHER_ADDR_LEN];
	char			sc_alpha2[4]; /* regulatory-domain */
	int			sc_reg;	/* enum mwx_reg (0.7.1) */

	int16_t			sc_coverage_class;
	uint8_t			sc_slottime;

	/* mac specific */
	uint32_t		sc_rxfilter;

	/* diagnostic counters, all rate-limited at print sites */
	uint32_t		sc_rx_process_events;
	uint32_t		sc_rx_pkt_event;
	uint32_t		sc_rx_pkt_txrx_notify;
	uint32_t		sc_rx_pkt_normal_mcu;
	uint32_t		sc_rx_pkt_normal;
	uint32_t		sc_rx_pkt_unknown;
	uint32_t		sc_rx_fill_fail;
	uint32_t		sc_rx_delivered;
	uint32_t		sc_scan_done_count;
	uint32_t		sc_scan_last_ieee80211_nodes;

	/* YYavic RX A-MSDU: subframes split by the hardware (MDP_DCR0_DAMSDU_EN)
	 * waiting to be joined back into one A-MSDU MPDU (mwx_amsdu_*). */
#define	MWX_AMSDU_MAX_SUB	16
	struct mbuf		*sc_amsdu_sub[MWX_AMSDU_MAX_SUB];
	uint16_t		 sc_amsdu_len[MWX_AMSDU_MAX_SUB];	/* subframe bytes (14 + payload) */
	int			 sc_amsdu_n;
	u_int			 sc_amsdu_hdrlen;
	uint16_t		 sc_amsdu_seq;
	uint8_t			 sc_amsdu_ta[IEEE80211_ADDR_LEN];
	struct ieee80211_rxinfo	 sc_amsdu_rxi;
	int			 sc_amsdu_idle;	/* RX batches / polls without a new subframe */
};

const uint8_t mwx_channels_2ghz[] = {
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14
};

#define MWX_NUM_2GHZ_CHANNELS   nitems(mwx_channels_2ghz)

const uint8_t mwx_channels_5ghz[] = {
	36, 40, 44, 48, 52, 56, 60, 64,
	100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144,
	149, 153, 157, 161, 165, 169, 173
};

#define MWX_NUM_5GHZ_CHANNELS   nitems(mwx_channels_5ghz)

const uint8_t mwx_channels_6ghz[] = {
	/* UNII-5 */
	1, 5, 9, 13, 17, 21, 25, 29, 33, 37, 41, 45, 49, 53, 57,
	61, 65, 69, 73, 77, 81, 85, 89, 93,
	/* UNII-6 */
	97, 101, 105, 109, 113, 117,
	/* UNII-7 */
	121, 125, 129, 133, 137, 141, 145, 149, 153, 157, 161, 165, 169,
	173, 177, 181, 185,
	/* UNII-8 */
	189, 193, 197, 201, 205, 209, 213, 217, 221, 225, 229, 233
};

const struct mwx_rate {
	uint16_t	rate;
	uint16_t	hw_value;
} mt76_rates[] = {
	{	2,	(MT_PHY_TYPE_CCK << 8) | 0 },
	{	4,	(MT_PHY_TYPE_CCK << 8) | 1 },
	{	11,	(MT_PHY_TYPE_CCK << 8) | 2 },
	{	22,	(MT_PHY_TYPE_CCK << 8) | 3 },
	{	12,	(MT_PHY_TYPE_OFDM << 8) | 11 },
	{	18,	(MT_PHY_TYPE_OFDM << 8) | 15 },
	{	24,	(MT_PHY_TYPE_OFDM << 8) | 10 },
	{	36,	(MT_PHY_TYPE_OFDM << 8) | 14 },
	{	48,	(MT_PHY_TYPE_OFDM << 8) | 9 },
	{	72,	(MT_PHY_TYPE_OFDM << 8) | 13 },
	{	96,	(MT_PHY_TYPE_OFDM << 8) | 8 },
	{	108,	(MT_PHY_TYPE_OFDM << 8) | 12 },
};


#define MWX_NUM_6GHZ_CHANNELS   nitems(mwx_channels_6ghz)

#define MWX_RATE_HT_SISO_MCS_INV_PLCP 0x20

#define	DEVNAME(s)	((s)->sc_dev.dv_xname)

/*
 * Regulatory channel sets (Yusufcan Yavic, 0.7.1; 0.7.0 and earlier used the
 * Turkish set in the native layer only, while the radio scanned all of its
 * channels). The country is the boot-arg yyavic-cc=XX (ISO 3166 alpha-2).
 * Without it, or for a country not listed below, only the channels allowed
 * in all of these regions are used ("00"). The country code is also what the
 * firmware gets in the channel domain and TX power commands, as Linux sends
 * the regdomain's alpha2. 20 MHz channel numbers; the radio's scan is passive.
 */
enum mwx_reg { MWX_REG_WORLD, MWX_REG_ETSI, MWX_REG_FCC, MWX_REG_JP, MWX_REG_CN };
static const char *const mwx_reg_names[] = { "world", "ETSI", "FCC", "JP", "CN" };
/* EU, EEA, CH, GB, TR: 2.4 GHz 1-13, 5 GHz 36-64 and 100-140 */
static const char mwx_reg_etsi[] =
    "AT BE BG CH CY CZ DE DK EE ES FI FR GB GR HR HU IE IS IT LI LT LU LV MT "
    "NL NO PL PT RO SE SI SK TR";

static int
mwx_reg_of(const char *cc)
{
	const char *p;

	if (cc[0] == 'U' && cc[1] == 'S')
		return MWX_REG_FCC;		/* 1-11, 36-64, 100-144, 149-165 */
	if (cc[0] == 'P' && cc[1] == 'R')
		return MWX_REG_FCC;
	if (cc[0] == 'J' && cc[1] == 'P')
		return MWX_REG_JP;		/* 1-13, 36-64, 100-140 */
	if (cc[0] == 'C' && cc[1] == 'N')
		return MWX_REG_CN;		/* 1-13, 36-64, 149-165 */
	for (p = mwx_reg_etsi; p[0] != '\0' && p[1] != '\0'; p += 3) {
		if (p[0] == cc[0] && p[1] == cc[1])
			return MWX_REG_ETSI;
		if (p[2] == '\0')
			break;
	}
	return MWX_REG_WORLD;			/* 1-11, 36-64 */
}

static int
mwx_reg_allowed(int reg, int is5, int ch)
{
	if (!is5)
		return ch >= 1 && ch <= ((reg == MWX_REG_WORLD ||
		    reg == MWX_REG_FCC) ? 11 : 13);
	if (ch >= 36 && ch <= 64)
		return 1;
	switch (reg) {
	case MWX_REG_ETSI:
	case MWX_REG_JP:
		return ch >= 100 && ch <= 140;
	case MWX_REG_FCC:
		return (ch >= 100 && ch <= 144) || (ch >= 149 && ch <= 165);
	case MWX_REG_CN:
		return ch >= 149 && ch <= 165;
	default:
		return 0;
	}
}

/* Country code (two letters, upper case) or "00"; sets sc_alpha2 and sc_reg. */
static void
mwx_reg_set(struct mwx_softc *sc, const char *cc)
{
	char a = cc[0], b = a != '\0' ? cc[1] : '\0';

	if (a >= 'a' && a <= 'z')
		a -= 'a' - 'A';
	if (b >= 'a' && b <= 'z')
		b -= 'a' - 'A';
	if (a < 'A' || a > 'Z' || b < 'A' || b > 'Z') {
		a = '0';
		b = '0';
	}
	sc->sc_alpha2[0] = a;
	sc->sc_alpha2[1] = b;
	sc->sc_alpha2[2] = '\0';
	sc->sc_alpha2[3] = '\0';
	sc->sc_reg = a == '0' ? MWX_REG_WORLD : mwx_reg_of(sc->sc_alpha2);
}

/* Fill ic_channels with the allowed channels (the others are cleared). Also
 * used at run time (dbg 37, net80211 in INIT state), hence the channel_init /
 * setmode that rebuild net80211's available and active channel sets. */
static void
mwx_reg_channels(struct mwx_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	unsigned int i;
	int chan;

	for (i = 0; i <= IEEE80211_CHAN_MAX; i++) {
		ic->ic_channels[i].ic_freq = 0;
		ic->ic_channels[i].ic_flags = 0;
	}
	if (sc->sc_capa.has_2ghz) {
		for (i = 0; i < MWX_NUM_2GHZ_CHANNELS; i++) {
			chan = mwx_channels_2ghz[i];
			if (!mwx_reg_allowed(sc->sc_reg, 0, chan))
				continue;
			ic->ic_channels[chan].ic_freq =
			    ieee80211_ieee2mhz(chan, IEEE80211_CHAN_2GHZ);
			ic->ic_channels[chan].ic_flags =
			    IEEE80211_CHAN_CCK | IEEE80211_CHAN_OFDM |
			    IEEE80211_CHAN_DYN | IEEE80211_CHAN_2GHZ |
			    IEEE80211_CHAN_HT20;	/* R9T trace27 */
		}
	}
	if (sc->sc_capa.has_5ghz) {
		for (i = 0; i < MWX_NUM_5GHZ_CHANNELS; i++) {
			chan = mwx_channels_5ghz[i];
			if (!mwx_reg_allowed(sc->sc_reg, 1, chan))
				continue;
			ic->ic_channels[chan].ic_freq =
			    ieee80211_ieee2mhz(chan, IEEE80211_CHAN_5GHZ);
			ic->ic_channels[chan].ic_flags = IEEE80211_CHAN_A |
			    IEEE80211_CHAN_HT20 | IEEE80211_CHAN_HT40 |	/* R9T trace27/33 */
			    IEEE80211_CHAN_VHT20 | IEEE80211_CHAN_VHT40 |
			    IEEE80211_CHAN_VHT80;
		}
	}
	MWX_DEV_LOG(sc, "regulatory country=%s region=%s\n",
	    sc->sc_alpha2, mwx_reg_names[sc->sc_reg]);
}
#define	DEVDEBUG(x)	((x)->sc_ic.ic_if.if_flags & IFF_DEBUG)

#define MWX_RX_DROP_LOG(sc, reason, rv, rxd0, rxd1, rxd2, rxd3, rxd4) do { \
	if ((sc)->sc_rx_fill_fail < 64) { \
		MWX_DEV_LOG((sc), \
		    "rx drop reason=%s rv=%d rxd=%08x/%08x/%08x/%08x/%08x len=%zu\n", \
		    (reason), (rv), (rxd0), (rxd1), (rxd2), (rxd3), \
		    (rxd4), mbuf_len(m)); \
	} \
} while (0)

#ifdef MWX_DEBUG
#define	DPRINTF(x...)		MWX_LOG(x)
#else
#define	DPRINTF(x...)
#endif

#ifdef MWX_PACKET_HEXDUMP
static void
pkt_hex_dump(struct mbuf *m)
{
	int len, rowsize = 16;
	int i, l, linelen;
	uint8_t *data;

	smtw_trace("Packet hex dump:\n");
	data = mtod(m, uint8_t *);
	len = mbuf_len(m);

	for (i = 0; i < len; i += linelen) {
		smtw_trace("%04x\t", i);
		linelen = len - i;
		if (len - i > rowsize)
			linelen = rowsize;
		for (l = 0; l < linelen; l++)
			smtw_trace("%02X ", (uint32_t)data[l]);
		data += linelen;
		smtw_trace("\n");
	}
}
#endif

int	mwx_init(struct ifnet *);
void	mwx_stop(struct ifnet *);
void	mwx_watchdog(struct ifnet *);
void	mwx_start(struct ifnet *);
static void	mwx_tx_kick(struct mwx_softc *);
static inline int	mwx_dma_free_slots(struct mwx_queue *);
int	mwx_ioctl(struct ifnet *, u_long, caddr_t);

struct ieee80211_node *mwx_node_alloc(struct ieee80211com *);
int	mwx_media_change(struct ifnet *);
#if NBPFILTER > 0
void	mwx_radiotap_attach(struct mwx_softc *);
#endif

int	mwx_newstate(struct ieee80211com *, enum ieee80211_state, int);
void	mwx_newstate_task(void *);
int	mwx_scan(struct mwx_softc *);
int	mwx_bgscan(struct ieee80211com *);
void	mwx_bgscan_done(struct ieee80211com *,
	    struct ieee80211_node_switch_bss_arg *, size_t);
void	mwx_bgscan_done_task(void *);
int	mwx_set_key(struct ieee80211com *, struct ieee80211_node *,
	    struct ieee80211_key *);
void	mwx_delete_key(struct ieee80211com *, struct ieee80211_node *,
	    struct ieee80211_key *);
void	mwx_setkey_task(void *);

int	mwx_tx(struct mwx_softc *, struct mbuf *, struct ieee80211_node *);
void	mwx_rx(struct mwx_softc *, struct mbuf *, struct mbuf_list *);
int	mwx_count_nodes(struct ieee80211com *);
int	mwx_intr(void *);
int	mwx_intr_body(void *);
int	mwx_preinit(struct mwx_softc *);
void	mwx_attach_hook(struct device *);
bool	mwx_attach(struct device *, struct device *, void *);
int	mwx_activate(struct device *, int);

void	mwx_reset(struct mwx_softc *);
void	mwx_reset_timeo(void *);
void	mwx_reset_task(void *);
int	mwx_txwi_alloc(struct mwx_softc *, int);
void	mwx_txwi_free(struct mwx_softc *);
struct mwx_txwi	*mwx_txwi_get(struct mwx_softc *);
void	mwx_txwi_put(struct mwx_softc *, struct mwx_txwi *);
int	mwx_txwi_enqueue(struct mwx_softc *, struct mwx_txwi *, struct mbuf **);
int	mwx_queue_alloc(struct mwx_softc *, struct mwx_queue *, int, uint32_t);
void	mwx_queue_free(struct mwx_softc *, struct mwx_queue *);
void	mwx_queue_reset(struct mwx_softc *, struct mwx_queue *);
int	mwx_buf_fill(struct mwx_softc *, struct mwx_queue_data *,
	    struct mwx_desc *);
int	mwx_queue_fill(struct mwx_softc *, struct mwx_queue *);
int	mwx_dma_alloc(struct mwx_softc *);
int	mwx_dma_reset(struct mwx_softc *, int);
void	mwx_dma_free(struct mwx_softc *);
int	mwx_dma_tx_enqueue(struct mwx_softc *, struct mwx_queue *,
	    struct mbuf *);
int	mwx_dma_txwi_enqueue(struct mwx_softc *, struct mwx_queue *,
	    struct mwx_txwi *);
void	mwx_dma_tx_cleanup(struct mwx_softc *, struct mwx_queue *);
void	mwx_dma_tx_done(struct mwx_softc *);
void	mwx_dma_rx_process(struct mwx_softc *, struct mbuf_list *);
void	mwx_dma_rx_dequeue(struct mwx_softc *, struct mwx_queue *,
	    struct mbuf_list *);
void	mwx_dma_rx_done(struct mwx_softc *, struct mwx_queue *);

struct mbuf	*mwx_mcu_alloc_msg(size_t);
void	mwx_mcu_set_len(struct mbuf *, void *);
int	mwx_mcu_send_mbuf(struct mwx_softc *, uint32_t, struct mbuf *, int *);
int	mwx_mcu_send_msg(struct mwx_softc *, uint32_t, void *, size_t, int *);
int	mwx_mcu_send_wait(struct mwx_softc *, uint32_t, void *, size_t);
int	mwx_mcu_send_mbuf_wait(struct mwx_softc *, uint32_t, struct mbuf *);
void	mwx_mcu_rx_event(struct mwx_softc *, struct mbuf *);
int	mwx_mcu_wait_resp_int(struct mwx_softc *, uint32_t, int, uint32_t *);
int	mwx_mcu_wait_resp_msg(struct mwx_softc *, uint32_t, int,
	    struct mbuf **);

int		mwx_dma_disable(struct mwx_softc *sc, int force);
void		mwx_dma_enable(struct mwx_softc *sc);
int		mwx_mcu_fw_pmctrl(struct mwx_softc *);
int		mwx_mcu_drv_pmctrl(struct mwx_softc *);
int		mwx_wfsys_reset(struct mwx_softc *sc);
uint32_t	mwx_reg_addr(struct mwx_softc *, uint32_t);
int		mwx_init_hardware(struct mwx_softc *);
int		mt7921_mcu_init(struct mwx_softc *);
int		mwx_load_firmware(struct mwx_softc *);
int		mwx_mac_wtbl_update(struct mwx_softc *, int);
void		mwx_mac_init_band(struct mwx_softc *sc, uint32_t);
void		mwx_mac_init(struct mwx_softc *);
int		mwx_mcu_patch_sem_ctrl(struct mwx_softc *, int);
int		mwx_mcu_init_download(struct mwx_softc *, uint32_t,
		    uint32_t, uint32_t);
int		mwx_mcu_send_firmware(struct mwx_softc *, int,
		    u_char *, size_t, size_t);
int		mwx_mcu_start_patch(struct mwx_softc *);
int		mwx_mcu_start_firmware(struct mwx_softc *, uint32_t,
		    uint32_t);
int		mt7921_mcu_get_nic_capability(struct mwx_softc *);
int		mt7921_mcu_fw_log_2_host(struct mwx_softc *, uint8_t);
int		mt7921_mcu_set_eeprom(struct mwx_softc *);
int		mt7921_mcu_set_rts_thresh(struct mwx_softc *, uint32_t,
		    uint8_t);
int		mwx_mcu_set_deep_sleep(struct mwx_softc *, int);
void		mt7921_mcu_low_power_event(struct mwx_softc *, struct mbuf *);
void		mt7921_mcu_tx_done_event(struct mwx_softc *, struct mbuf *);
void		mwx_end_scan_task(void *);
void		mt7921_mcu_scan_event(struct mwx_softc *, struct mbuf *);
int		mt7921_mcu_hw_scan(struct mwx_softc *, int);
int		mt7921_mcu_hw_scan_cancel(struct mwx_softc *);
int		mt7921_mcu_set_mac_enable(struct mwx_softc *, int, int);
int		mt7921_mcu_set_channel_domain(struct mwx_softc *);
uint8_t		mt7921_mcu_chan_bw(struct ieee80211_channel *channel);
static uint8_t	mwx_bss_bw(struct mwx_softc *, uint8_t *);
int		mt7921_mcu_set_chan_info(struct mwx_softc *, int);
void		mt7921_mcu_build_sku(struct mwx_softc *, int, int8_t *);
int		mt7921_mcu_rate_txpower_band(struct mwx_softc *, int,
		    const uint8_t *, int, int);
int		mt7921_mcu_set_rate_txpower(struct mwx_softc *);
void		mt7921_mac_reset_counters(struct mwx_softc *);
void		mt7921_mac_set_timing(struct mwx_softc *);
int		mt7921_mcu_uni_add_dev(struct mwx_softc *, struct mwx_vif *,
		    struct mwx_node *, int);
int		mt7921_mcu_set_sniffer(struct mwx_softc *, int);
int		mt7921_mcu_set_beacon_filter(struct mwx_softc *, int);
int		mt7921_mcu_set_bss_pm(struct mwx_softc *, int);
int		mt7921_mcu_set_tx(struct mwx_softc *, struct mwx_vif *);
int		mt7921_mac_fill_rx(struct mwx_softc *, struct mbuf *,
		    struct ieee80211_rxinfo *);
uint32_t	mt7921_mac_tx_rate_val(struct mwx_softc *);
void		mt7921_mac_write_txwi_80211(struct mwx_softc *, struct mbuf *,
		    struct ieee80211_node *, struct mt76_txwi *);
void		mt7921_mac_write_txwi(struct mwx_softc *, struct mbuf *,
		    struct ieee80211_node *, struct mt76_txwi *);
void		mwx_mac_tx_free(struct mwx_softc *, struct mbuf *);
int		mt7921_set_channel(struct mwx_softc *);
int mt7921_mcu_set_associated_bss(struct mwx_softc *, int);
int mt7921_mcu_set_associated_peer(struct mwx_softc *);

uint8_t		 mt7921_get_phy_mode_v2(struct mwx_softc *,
		    struct ieee80211_node *);
struct mbuf	*mwx_alloc_sta_req_tlv(int);
void		*mwx_append_len(struct mbuf *, int);
void		*mwx_append_tlv(struct mbuf *, uint16_t *, int, int);
struct mbuf	*mwx_fill_sta_req_hdr(struct mbuf *, struct mwx_vif *,
		    uint8_t, uint16_t, uint16_t);
void		 mt7921_mcu_add_basic_tlv(struct mbuf *, uint16_t *,
		    struct mwx_softc *, struct ieee80211_node *, int, int);
void		 mt7921_mcu_add_sta_tlv(struct mbuf *, uint16_t *,
		    struct mwx_softc *, struct ieee80211_node *, int, int);
int		 mt7921_mcu_wtbl_generic_tlv(struct mbuf *, uint16_t *,
		    struct mwx_softc *, struct ieee80211_node *);
int		 mt7921_mcu_wtbl_hdr_trans_tlv(struct mbuf *, uint16_t *,
		    struct mwx_softc *, struct ieee80211_node *);
int		 mt7921_mcu_wtbl_ht_tlv(struct mbuf *, uint16_t *,
		    struct mwx_softc *, struct ieee80211_node *);
int		 mt7921_mac_sta_update(struct mwx_softc *,
		    struct ieee80211_node *, int, int);
void		 mt7921_mcu_add_key_tlv(struct mbuf *, uint16_t *,
		    struct ieee80211_key *, int);
int		 mt7921_mcu_sta_key_update(struct mwx_softc *,
		    struct ieee80211_node *, struct ieee80211_key *);
void		 mt7921_mcu_sta_key_delete(struct mwx_softc *,
			    struct ieee80211_node *, struct ieee80211_key *);

static inline int
mwx_dbm_to_rssi(int dbm)
{
	if (dbm <= -100)
		return 0;
	if (dbm >= 0)
		return 100;
	return dbm + 100;
}

static inline uint32_t
mwx_read(struct mwx_softc *sc, uint32_t reg)
{
	reg = mwx_reg_addr(sc, reg);
	return bus_space_read_4(sc->sc_st, sc->sc_memh, reg);
}

static inline void
mwx_write(struct mwx_softc *sc, uint32_t reg, uint32_t val)
{
	reg = mwx_reg_addr(sc, reg);
	bus_space_write_4(sc->sc_st, sc->sc_memh, reg, val);
}

static inline void
mwx_barrier(struct mwx_softc *sc)
{
	bus_space_barrier(sc->sc_st, sc->sc_memh, 0, sc->sc_mems,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
}

static inline uint32_t
mwx_rmw(struct mwx_softc *sc, uint32_t reg, uint32_t val, uint32_t mask)
{
	reg = mwx_reg_addr(sc, reg);
	val |= bus_space_read_4(sc->sc_st, sc->sc_memh, reg) & ~mask;
	bus_space_write_4(sc->sc_st, sc->sc_memh, reg, val);
	return val;
}

static inline uint32_t
mwx_set(struct mwx_softc *sc, uint32_t reg, uint32_t bits)
{
	return mwx_rmw(sc, reg, bits, 0);
}

static inline uint32_t
mwx_clear(struct mwx_softc *sc, uint32_t reg, uint32_t bits)
{
	return mwx_rmw(sc, reg, 0, bits);
}

static inline uint32_t
mt7921_map_reg_l1(struct mwx_softc *sc, uint32_t reg)
{
	uint32_t offset = MWX_HIF_REG_OFFSET(reg);
	uint32_t base = MWX_HIF_REG_BASE(reg);

	mwx_rmw(sc, MT7921_HIF_REMAP_L1,
	    base << MT7921_HIF_REMAP_L1_SHIFT, MT7921_HIF_REMAP_L1_MASK);
	mwx_barrier(sc);

	return MT7921_HIF_REMAP_BASE_L1 + offset;
}

/*
 * Poll for timeout milliseconds or until register reg read the value val
 * after applying the mask to the value read. Returns 0 on success ETIMEDOUT
 * on failure.
 */
int
mwx_poll(struct mwx_softc *sc, uint32_t reg, uint32_t val, uint32_t mask,
    int timeout)
{
	uint32_t cur;

	reg = mwx_reg_addr(sc, reg);
	timeout *= 100;
	do {
		cur = bus_space_read_4(sc->sc_st, sc->sc_memh, reg) & mask;
		if (cur == val)
			return 0;
		delay(10);
	} while (timeout-- > 0);

	smtw_trace("%s: poll timeout bar_off=0x%x val=0x%x mask=0x%x cur=0x%x raw=0x%08x\n",
	    DEVNAME(sc), reg, val, mask, cur,
	    bus_space_read_4(sc->sc_st, sc->sc_memh, reg));
	return ETIMEDOUT;
}

/*
 * ifp specific functions
 */
int
mwx_init(struct ifnet *ifp)
{
	struct mwx_softc *sc = (struct mwx_softc *)ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct mwx_node *mn;
	int rv;

	if (!sc->sc_fw_loaded) {
		rv = mwx_preinit(sc);
		if (rv) {
			MWX_DEV_LOG(sc, "init preinit failed error=%d\n", rv);
			return rv;
		}
	}

	MWX_DEV_LOG(sc, "init start opmode=%d flags=0x%x ic_state=%d\n",
	    ic->ic_opmode, ifp->if_flags, ic->ic_state);
	rv = mwx_mcu_set_deep_sleep(sc, 0);
	if (rv)
		MWX_DEV_LOG(sc, "init disable deep sleep returned %d\n", rv);
	else
		MWX_DEV_LOG(sc, "init deep sleep disabled\n");

	rv = mt7921_mcu_set_mac_enable(sc, 0, 1);
	if (rv) {
		MWX_DEV_LOG(sc, "init mac enable failed error=%d\n", rv);
		return rv;
	}
	MWX_DEV_LOG(sc, "init mac enable ok\n");

	rv = mt7921_mcu_set_channel_domain(sc);
	if (rv) {
		MWX_DEV_LOG(sc, "init channel domain failed error=%d\n", rv);
		return rv;
	}
	MWX_DEV_LOG(sc, "init channel domain ok\n");

#if 0
	/* XXX no channel available yet */
	rv = mt7921_mcu_set_chan_info(sc, MCU_EXT_CMD_SET_RX_PATH);
	if (rv)
		return rv;
#endif

	rv = mt7921_mcu_set_rate_txpower(sc);
	if (rv) {
		MWX_DEV_LOG(sc, "init rate txpower failed error=%d\n", rv);
		return rv;
	}
	MWX_DEV_LOG(sc, "init rate txpower ok\n");

	mt7921_mac_reset_counters(sc);
	MWX_DEV_LOG(sc, "init counters reset\n");

	mn = &sc->sc_vif.vif_mn;
	MWX_DEV_LOG(sc, "init vif node=%p wcid=%u\n", mn,
	    mn != NULL ? mn->wcid : 0);

	mwx_mac_wtbl_update(sc, mn->wcid);
	MWX_DEV_LOG(sc, "init wtbl updated wcid=%u\n", mn->wcid);

	rv = mt7921_mcu_uni_add_dev(sc, &sc->sc_vif, mn, 1);
	if (rv) {
		MWX_DEV_LOG(sc, "init add dev failed error=%d\n", rv);
		return rv;
	}
	MWX_DEV_LOG(sc, "init add dev ok\n");

	rv = mt7921_mcu_set_tx(sc, &sc->sc_vif);
	if (rv) {
		MWX_DEV_LOG(sc, "init set tx failed error=%d\n", rv);
		return rv;
	}
	MWX_DEV_LOG(sc, "init set tx ok\n");

	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		rv = mt7921_mcu_set_chan_info(sc, MCU_EXT_CMD_SET_RX_PATH);
		if (rv)
			return rv;
		rv = mt7921_set_channel(sc);
		if (rv)
			return rv;

		rv = mt7921_mcu_set_sniffer(sc, 1);
		MWX_DEV_LOG(sc, "init monitor sniffer enable returned %d\n", rv);
		rv = mt7921_mcu_set_beacon_filter(sc, 0);
		MWX_DEV_LOG(sc, "init monitor beacon filter disable returned %d\n", rv);
		sc->sc_rxfilter = 0;
		mwx_set(sc, MT_DMA_DCR0(0), MT_DMA_DCR0_RXD_G5_EN);
	} else {
		rv = mt7921_mcu_set_sniffer(sc, 0);
		MWX_DEV_LOG(sc, "init sniffer disable returned %d\n", rv);
		sc->sc_rxfilter |= MT_WF_RFCR_DROP_OTHER_UC;
		mwx_clear(sc, MT_DMA_DCR0(0), MT_DMA_DCR0_RXD_G5_EN);
	}
	mwx_write(sc, MT_WF_RFCR(0), sc->sc_rxfilter);
	MWX_DEV_LOG(sc, "init rxfilter set 0x%08x\n", sc->sc_rxfilter);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	MWX_DEV_LOG(sc, "init interface running flags=0x%x\n", ifp->if_flags);

	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		ic->ic_bss->ni_chan = ic->ic_ibss_chan;
		ieee80211_new_state(ic, IEEE80211_S_RUN, -1);
		MWX_DEV_LOG(sc, "init monitor state run\n");
		return 0;
	}

	MWX_DEV_LOG(sc, "init begin scan\n");
	ieee80211_begin_scan(ifp);
	MWX_DEV_LOG(sc, "init begin scan returned state=%d flags=0x%x sc_flags=0x%x\n",
	    ic->ic_state, ifp->if_flags, sc->sc_flags);

	/*
	 * ieee80211_begin_scan() ends up scheduling mwx_newstate_task().
	 * Wait until the transition to SCAN state has completed.
	 */

	return 0;
}

void
mwx_stop(struct ifnet *ifp)
{
	struct mwx_softc *sc = (struct mwx_softc *)ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	int s = splnet();
	int was_running = (ifp->if_flags & IFF_RUNNING) != 0;

	DPRINTF("%s: stop\n", DEVNAME(sc));

	/* Cancel scheduled tasks and let any stale tasks finish up. */
	if (sc->sc_nswq != NULL) {
		task_del(sc->sc_nswq, &sc->sc_newstate_task);
		task_del(sc->sc_nswq, &sc->sc_bgscan_done_task);
		task_del(sc->sc_nswq, &sc->sc_setkey_task);
		task_del(sc->sc_nswq, &sc->sc_reset_task);
	}
	task_del(systq, &sc->sc_scan_task);
	timeout_del(&sc->sc_reset_to);

	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	if (was_running) {
		struct mwx_node *mn;

		free(sc->sc_bgscan_arg, M_DEVBUF, sc->sc_bgscan_arg_size);
		sc->sc_bgscan_arg = NULL;
		sc->sc_bgscan_arg_size = 0;
		if (sc->sc_flags & MWX_FLAG_SCANNING) {
			mt7921_mcu_hw_scan_cancel(sc);
			sc->sc_flags &= ~MWX_FLAG_SCANNING;
		}

		mn = &sc->sc_vif.vif_mn;
		mt7921_mcu_uni_add_dev(sc, &sc->sc_vif, mn, 0);
		mwx_mcu_set_deep_sleep(sc, 1);
		mt7921_mcu_set_mac_enable(sc, 0, 0);
	}

	sc->sc_newstate(ic, IEEE80211_S_INIT, -1);
	sc->sc_ns_state = IEEE80211_S_INIT;
	ifp->if_timer = 0;

	splx(s);
}

void
mwx_watchdog(struct ifnet *ifp)
{
	ifp->if_timer = 0;
	ieee80211_watchdog(ifp);
}

static void mwx_start_body(struct ifnet *ifp);

void
mwx_start(struct ifnet *ifp)
{
	uint64_t t0 = mach_absolute_time();
	mwx_start_body(ifp);
	gYYPfStart += mach_absolute_time() - t0;
	gYYPfStartN++;
}

static void
mwx_start_body(struct ifnet *ifp)
{
	struct mwx_softc *sc = (struct mwx_softc *)ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	struct ether_header *eh;
	struct mbuf *m;

	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;

	for (;;) {
		/* XXX TODO handle oactive
			ifq_set_oactive(&ifp->if_snd);
		*/

		/* need to send management frames even if we're not RUNning */
		m = mq_dequeue(&ic->ic_mgtq);
		if (m) {
			ni = (struct ieee80211_node *)mbuf_pkthdr_rcvif(m);
			goto sendit;
		}

		if (ic->ic_state != IEEE80211_S_RUN ||
		    (ic->ic_xflags & IEEE80211_F_TX_MGMT_ONLY))
			break;

		/* R9T trace34: kaynak yoksa paketi atma, kuyrukta beklet (OACTIVE) */
		if (gTxBackpressure &&
		    (LIST_EMPTY(&sc->sc_txwi.mt_freelist) ||
		    mwx_dma_free_slots(&sc->sc_txq) < 2)) {
			ifq_set_oactive(&ifp->if_snd);
			gTxOactSet++;
			break;
		}
		m = ifq_dequeue(&ifp->if_snd);
		if (!m)
			break;
		gTxDeq++;
		if (mbuf_len(m) < sizeof (*eh) &&
		    (m = m_pullup(m, sizeof (*eh))) == NULL) {
			ifp->if_oerrors++;
			continue;
		}
#if NBPFILTER > 0
		if (ifp->if_bpf != NULL)
			bpf_mtap(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif
		if ((m = ieee80211_encap(ifp, m, &ni)) == NULL) {
			ifp->if_oerrors++;
			continue;
		}

 sendit:
#if NBPFILTER > 0
		if (ic->ic_rawbpf != NULL)
			bpf_mtap(ic->ic_rawbpf, m, BPF_DIRECTION_OUT);
#endif
		int rv = mwx_tx(sc, m, ni);
		/* mwx_tx copies node metadata into the descriptor; completion uses
		 * the packet/token only, so release the enqueue reference here. */
		ieee80211_release_node(ic, ni);
		if (rv != 0) {
			ifp->if_oerrors++;
			continue;
		}

		if (ifp->if_flags & IFF_UP)
			ifp->if_timer = 1;
	}
}

int
mwx_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	int s, err = 0;

	s = splnet();
	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		/* FALLTHROUGH */
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (!(ifp->if_flags & IFF_RUNNING)) {
				mwx_stop(ifp);
				err = mwx_init(ifp);
			}
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				mwx_stop(ifp);
		}
		break;
	default:
		err = ieee80211_ioctl(ifp, cmd, data);
		break;
	}

	if (err == ENETRESET) {
		err = 0;
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
		    (IFF_UP | IFF_RUNNING)) {
			mwx_stop(ifp);
			err = mwx_init(ifp);
		}
	}
	splx(s);
	return err;
}

int
mwx_media_change(struct ifnet *ifp)
{
	struct mwx_softc *sc = (struct mwx_softc *)ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	int err;

	err = ieee80211_media_change(ifp);
	if (err != ENETRESET)
		return err;

	/* TODO lot more handling here */
	if (ic->ic_fixed_mcs != -1) {
		;
	} else if (ic->ic_fixed_rate != -1) {
		;
	}
	if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
	    (IFF_UP | IFF_RUNNING)) {
		/* XXX could be a bit harsh */
		mwx_stop(ifp);
		err = mwx_init(ifp);
	}
	return err;
}

/*
 * net80211 specific functions.
 */

struct ieee80211_node *
mwx_node_alloc(struct ieee80211com *ic)
{
	/* XXX this is just wrong */
	static uint32_t wcid = 0;
	struct mwx_softc *sc = (struct mwx_softc *)ic->ic_softc;
	struct mwx_node *mn;

	mn = (struct mwx_node *)malloc(sizeof(struct mwx_node), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (mn == NULL)
		return NULL;
	mn->wcid = 1 + wcid++ % (MWX_WTBL_STA - 1);

	/* init WCID table entry */
	mwx_mac_wtbl_update(sc, mn->wcid);

	return &mn->ni;
}

void
mwx_newassoc(struct ieee80211com *ic, struct ieee80211_node *ni, int isnew)
{
	struct mwx_softc *sc = (struct mwx_softc *)ic->ic_softc;
	struct mwx_node *mn = (struct mwx_node *)ni;
	uint16_t wcid = 0;

	if (isnew && ni->ni_associd != 0) {
		/* only interested in true associations */
		wcid = IEEE80211_AID(ni->ni_associd);

	}
	smtw_trace("%s: new assoc isnew=%d addr=%s WCID=%d\n", DEVNAME(sc),
	    isnew, ether_sprintf(ni->ni_macaddr), mn->wcid);

	/* XXX TODO rate handling here */
}

#ifndef IEEE80211_STA_ONLY
void
mwx_node_leave(struct ieee80211com *ic, struct ieee80211_node *ni)
{
#if 0
	struct mwx_softc *sc = (struct mwx_softc *)ic->ic_softc;
	struct mwx_node *mn = (struct mwx_node *)ni;
	uint16_t wcid = mn->wcid;

	/* TODO clear WCID */
#endif
}
#endif

int
mwx_scan(struct mwx_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	int rv;

	if (sc->sc_flags & MWX_FLAG_BGSCAN) {
		rv = mt7921_mcu_hw_scan_cancel(sc);
		if (rv) {
			smtw_trace("%s: could not abort background scan\n",
			    DEVNAME(sc));
			return rv;
		}
	}

	rv = mt7921_mcu_hw_scan(sc, 0);
	if (rv) {
		smtw_trace("%s: could not initiate scan\n", DEVNAME(sc));
		return rv;
	}

	/*
	 * The current mode might have been fixed during association.
	 * Ensure all channels get scanned.
	 */
	if (IFM_MODE(ic->ic_media.ifm_cur->ifm_media) != IFM_AUTO)
		ieee80211_setmode(ic, IEEE80211_MODE_AUTO);

	sc->sc_flags |= MWX_FLAG_SCANNING;
	if (ifp->if_flags & IFF_DEBUG)
		smtw_trace("%s: %s -> %s\n", ifp->if_xname,
		    ieee80211_state_name[ic->ic_state],
		    ieee80211_state_name[IEEE80211_S_SCAN]);
	if ((sc->sc_flags & MWX_FLAG_BGSCAN) == 0) {
		ieee80211_set_link_state(ic, LINK_STATE_DOWN);
		ieee80211_node_cleanup(ic, ic->ic_bss);
	}
	ic->ic_state = IEEE80211_S_SCAN;

	return 0;
}

int
mwx_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct mwx_softc *sc = (struct mwx_softc *)ic->ic_softc;

	/*
	 * Prevent attempts to transition towards the same state, unless
	 * we are scanning in which case a SCAN -> SCAN transition
	 * triggers another scan iteration. And AUTH -> AUTH is needed
	 * to support band-steering.
	 */
	if (sc->sc_ns_state == nstate && nstate != IEEE80211_S_SCAN &&
	    nstate != IEEE80211_S_AUTH)
		return 0;

	if (ic->ic_state == IEEE80211_S_RUN) {
		/* cancel other tasks here */
	}

	sc->sc_ns_state = nstate;
	sc->sc_ns_arg = arg;

	task_add(sc->sc_nswq, &sc->sc_newstate_task);
	return 0;
}

void
mwx_newstate_task(void *ptr)
{
	struct mwx_softc *sc = (struct mwx_softc *)ptr;
	struct ieee80211com *ic = &sc->sc_ic;
	enum ieee80211_state ostate = ic->ic_state;
	enum ieee80211_state nstate = sc->sc_ns_state;
	int arg = sc->sc_ns_arg;
	int s = splnet();
	int rv = 0;

	switch (ostate) {
	case IEEE80211_S_RUN:
		if (nstate != ostate)
			mwx_mcu_set_deep_sleep(sc, 1);
		break;
	case IEEE80211_S_SCAN:
		if (nstate == ostate) {
			if (sc->sc_flags & MWX_FLAG_SCANNING) {
				splx(s);
				return;
			}
		}
		break;
	default:
		break;
	}

	DPRINTF("%s: %s %d -> %d\n", DEVNAME(sc), __func__, ostate, nstate);
	MWX_DEV_LOG(sc, "newstate task %d -> %d arg=%d\n", ostate, nstate, arg);

	switch (nstate) {
	case IEEE80211_S_INIT:
		break;
	case IEEE80211_S_SCAN:
		rv = mwx_scan(sc);
		if (rv)
			break;
		break;
	case IEEE80211_S_AUTH:
		rv = mt7921_set_channel(sc);
		if (rv)
			break;
		mwx_mcu_set_deep_sleep(sc, 0);
		mt7921_mac_sta_update(sc, sc->sc_ic.ic_bss, 1, 1);
		break;
	case IEEE80211_S_ASSOC:
		mwx_mcu_set_deep_sleep(sc, 1);
		break;
	case IEEE80211_S_RUN:
		if (ic->ic_opmode == IEEE80211_M_MONITOR)
			break;

		mt7921_mcu_hw_scan_cancel(sc);
		mwx_mcu_set_deep_sleep(sc, 0);
		/* R9T trace33: anlasilan genislikle (40/80 MHz) kanali yeniden ayarla */
		{
			uint8_t cc = 0;
			if (mwx_bss_bw(sc, &cc) != CMD_CBW_20MHZ) {
				int crv = mt7921_set_channel(sc);
				MWX_DEV_LOG(sc, "RUN set_channel wide rv=%d\n", crv);
			}
		}
		rv = mt7921_mcu_set_associated_bss(sc, 1);
		/* R9T trace27: assoc sonrasi STA_REC (HT/PHY/RA/WTBL_HT) guncelle */
		if (!rv) {
			int srv = mt7921_mac_sta_update(sc, ic->ic_bss, 1, 0);
			MWX_DEV_LOG(sc, "RUN sta_update ht=%d rv=%d\n",
			    (ic->ic_bss->ni_flags & IEEE80211_NODE_HT) != 0, srv);
		}
		if (!rv)
			rv = mt7921_mcu_set_associated_peer(sc);
		if (rv)
			break;
		mt7921_mcu_set_rts_thresh(sc, 0x92b, 0);
		break;
	}

	if (rv)
		mwx_reset(sc);
	else
		sc->sc_newstate(ic, nstate, arg);
	splx(s);
}

int
mwx_bgscan(struct ieee80211com *ic)
{
	struct mwx_softc *sc = (struct mwx_softc *)ic->ic_softc;
	int err;

	if (sc->sc_flags & MWX_FLAG_SCANNING)
		return 0;

	err = mt7921_mcu_hw_scan(sc, 1);
	if (err) {
		smtw_trace("%s: could not initiate scan\n", DEVNAME(sc));
		return err;
	}

	sc->sc_flags |= MWX_FLAG_BGSCAN;
	return 0;
}

void
mwx_bgscan_done(struct ieee80211com *ic,
    struct ieee80211_node_switch_bss_arg *arg, size_t arg_size)
{
	struct mwx_softc *sc = (struct mwx_softc *)ic->ic_softc;

	free(sc->sc_bgscan_arg, M_DEVBUF, sc->sc_bgscan_arg_size);
	sc->sc_bgscan_arg = arg;
	sc->sc_bgscan_arg_size = arg_size;
	task_add(sc->sc_nswq, &sc->sc_bgscan_done_task);
}

void
mwx_bgscan_done_task(void *arg)
{
	struct mwx_softc *sc = (struct mwx_softc *)arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = ic->ic_bss;
	int err = 0;
	int s = splnet();

	if (ic->ic_state != IEEE80211_S_RUN ||
	    (ic->ic_flags & IEEE80211_F_BGSCAN) == 0) {
		err = ENXIO;
		goto done;
	}

	/*
	 * Remove installed crypto keys while we still have access to them.
	 * Once mwx_newstate() is entered ic_bss will already contain
	 * information about our next AP.
	 */
	if (ic->ic_flags & IEEE80211_F_RSNON) {
		struct ieee80211_key *k;

		if (ni->ni_pairwise_key.k_cipher != IEEE80211_CIPHER_NONE)
			(*ic->ic_delete_key)(ic, ni, &ni->ni_pairwise_key);

		if (ic->ic_def_txkey == 1 || ic->ic_def_txkey == 2) {
			k = &ic->ic_nw_keys[ic->ic_def_txkey];
			if ((k->k_flags & IEEE80211_KEY_GROUP) &&
			    k->k_cipher == IEEE80211_CIPHER_CCMP)
				(*ic->ic_delete_key)(ic, ni, k);
		}

		if (ic->ic_igtk_kid == 4 || ic->ic_igtk_kid == 5) {
			k = &ic->ic_nw_keys[ic->ic_igtk_kid];
			if (k->k_flags & IEEE80211_KEY_IGTK)
				(*ic->ic_delete_key)(ic, ni, k);
		}

		ni->ni_port_valid = 0;
		ni->ni_flags &= ~IEEE80211_NODE_TXRXPROT;
		ni->ni_flags &= ~IEEE80211_NODE_TXMGMTPROT;
		ni->ni_flags &= ~IEEE80211_NODE_RXMGMTPROT;
		ni->ni_rsn_supp_state = RSNA_SUPP_INITIALIZE;
	}

	/*
	 * The local net80211 port switches BSS from ni_unref_cb when the
	 * current node's final reference is released. Install the same
	 * callback after the driver has removed hardware keys.
	 */
	ni->ni_unref_arg = sc->sc_bgscan_arg;
	ni->ni_unref_arg_size = sc->sc_bgscan_arg_size;
	ni->ni_unref_cb = ieee80211_node_switch_bss;
	sc->sc_bgscan_arg = NULL;
	sc->sc_bgscan_arg_size = 0;
	if (ni->ni_flags & IEEE80211_NODE_MFP)
		ieee80211_node_switch_bss(ic, ni);

 done:
	if (err) {
		free(sc->sc_bgscan_arg, M_DEVBUF, sc->sc_bgscan_arg_size);
		sc->sc_bgscan_arg = NULL;
		sc->sc_bgscan_arg_size = 0;
	}
	splx(s);
}

int
mwx_set_key(struct ieee80211com *ic, struct ieee80211_node *ni,
    struct ieee80211_key *k)
{
	struct mwx_softc *sc = (struct mwx_softc *)ic->ic_softc;
	struct mwx_setkey_task_arg *a;

	DPRINTF("%s: set_key: ni %p, k_id %d, k_flags %x k_cipher %d\n",
	    DEVNAME(sc), ni, k->k_id, k->k_flags, k->k_cipher);

	if ((sc->sc_setkey_cur + 1) % nitems(sc->sc_setkey_arg) ==
	    sc->sc_setkey_tail)
		return ENOSPC;

	a = &sc->sc_setkey_arg[sc->sc_setkey_cur];
	a->ni = ni;
	a->k = k;
	sc->sc_setkey_cur = (sc->sc_setkey_cur + 1) % nitems(sc->sc_setkey_arg);
	task_add(sc->sc_nswq, &sc->sc_setkey_task);
	return EBUSY;
}

void
mwx_delete_key(struct ieee80211com *ic, struct ieee80211_node *ni,
    struct ieee80211_key *k)
{
	struct mwx_softc *sc = (struct mwx_softc *)ic->ic_softc;

	DPRINTF("%s: delete_key: ni %p, k_id %d, k_flags %x k_cipher %d\n",
	    DEVNAME(sc), ni, k->k_id, k->k_flags, k->k_cipher);

	mt7921_mcu_sta_key_delete(sc, ni, k);
}

void
mwx_setkey_task(void *arg)
{
	struct mwx_softc *sc = (struct mwx_softc *)arg;
	struct mwx_setkey_task_arg *a;
	int s = splnet();

	while (sc->sc_setkey_tail != sc->sc_setkey_cur) {
		a = &sc->sc_setkey_arg[sc->sc_setkey_tail];
		mt7921_mcu_sta_key_update(sc, a->ni, a->k);
		a->ni = NULL;
		a->k = NULL;

		sc->sc_setkey_tail = (sc->sc_setkey_tail + 1) %
		    nitems(sc->sc_setkey_arg);
	}

	splx(s);
}

#if NBPFILTER > 0
void
mwx_radiotap_attach(struct mwx_softc *sc)
{
	bpfattach(&sc->sc_drvbpf, &sc->sc_ic.ic_if, DLT_IEEE802_11_RADIO,
	    sizeof(struct ieee80211_frame) + IEEE80211_RADIOTAP_HDRLEN);

	sc->sc_rxtap_len = sizeof sc->sc_rxtapu;
	sc->sc_rxtap.wr_ihdr.it_len = htole16(sc->sc_rxtap_len);
	sc->sc_rxtap.wr_ihdr.it_present = htole32(MWX_RX_RADIOTAP_PRESENT);

	sc->sc_txtap_len = sizeof sc->sc_txtapu;
	sc->sc_txtap.wt_ihdr.it_len = htole16(sc->sc_txtap_len);
	sc->sc_txtap.wt_ihdr.it_present = htole32(MWX_TX_RADIOTAP_PRESENT);
}
#endif

static void
mwx_diag_hex(struct mwx_softc *sc, const char *tag, const uint8_t *d, size_t len, size_t max)
{
	char line[100];
	size_t i, j;

	if (len > max)
		len = max;
	for (i = 0; i < len; i += 32) {
		size_t n = len - i < 32 ? len - i : 32;
		for (j = 0; j < n; j++)
			snprintf(line + j * 2, sizeof(line) - j * 2, "%02x", d[i + j]);
		line[n * 2] = 0;
		MWX_DEV_LOG(sc, "%s+%03zu %s\n", tag, i, line);
	}
}

int
mwx_tx(struct mwx_softc *sc, struct mbuf *m, struct ieee80211_node *ni)
{
	struct mwx_node *mn = (struct mwx_node *)ni;
	struct mwx_txwi *mt;
	struct mt76_txwi *txp;
	int rv;

	{
		static uint32_t dtx;
		struct ieee80211_frame *dwh = mtod(m, struct ieee80211_frame *);
		if ((dwh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) == IEEE80211_FC0_TYPE_MGT &&
		    (dwh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) != IEEE80211_FC0_SUBTYPE_PROBE_REQ && dtx < 40) {
			dtx++;
			MWX_DEV_LOG(sc, "DIAG TX mgmt subtype=0x%02x len=%zu state=%d\n",
			    dwh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK, mbuf_pkthdr_len(m), sc->sc_ic.ic_state);
			mwx_diag_hex(sc, "DIAG TX", (const uint8_t *)dwh, mbuf_len(m), 128);
		}
	}
	if ((mt = mwx_txwi_get(sc)) == NULL) {
		gTxNoTxwi++;
		m_freem(m);
		return ENOBUFS;
	}
	/* XXX DMA memory access without BUS_DMASYNC_PREWRITE */
	txp = mt->mt_desc;
	memset(txp, 0, sizeof(*txp));
	mt7921_mac_write_txwi(sc, m, ni, txp);

	rv = mwx_txwi_enqueue(sc, mt, &m);
	if (rv != 0) {
		gTxMapFail++;
		/* Mapping failed before ownership reached the TXWI. */
		m_freem(m);
		mwx_txwi_put(sc, mt);
		return rv;
	}

	static uint32_t tx_log_count;
	if (tx_log_count < 32) {
		MWX_DEV_LOG(sc, "tx frame wcid=%u pid=%d len=%zu\n",
		    mn->wcid, mt->mt_idx, mbuf_pkthdr_len(m));
		MWX_DEV_LOG(sc,
		    "txwi %08x %08x %08x %08x %08x %08x %08x %08x\n",
		    txp->txwi[0], txp->txwi[1], txp->txwi[2], txp->txwi[3],
		    txp->txwi[4], txp->txwi[5], txp->txwi[6], txp->txwi[7]);
		MWX_DEV_LOG(sc,
		    "tx desc msdu=%u/%u/%u/%u len=%u/%u/%u/%u\n",
		    txp->msdu_id[0], txp->msdu_id[1], txp->msdu_id[2],
		    txp->msdu_id[3], txp->ptr[0].len0, txp->ptr[0].len1,
		    txp->ptr[1].len0, txp->ptr[1].len1);
		tx_log_count++;
	}

	rv = mwx_dma_txwi_enqueue(sc, &sc->sc_txq, mt);
	if (rv != 0) {
		gTxRingFull++;
		mwx_txwi_put(sc, mt);
	} else
		gTxOk++;
	return rv;
}

/* Hand one received MPDU to net80211. */
static void
mwx_rx_deliver(struct mwx_softc *sc, struct mbuf *m, struct ieee80211_rxinfo *rxi,
    struct mbuf_list *ml)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_frame *wh = mtod(m, struct ieee80211_frame *);
	struct ieee80211_node *ni;

	/* grab a reference to the source node */
	ni = ieee80211_find_rxnode(ic, wh);

	if (ic->ic_max_rssi && rxi->rxi_rssi < 0)
		rxi->rxi_rssi = mwx_dbm_to_rssi(rxi->rxi_rssi);

	/* send the frame to the 802.11 layer */
	ieee80211_inputm(&ic->ic_if, m, ni, rxi, ml);

	/* node is no longer needed */
	ieee80211_release_node(ic, ni);
}

/*
 * YYavic RX A-MSDU: deliver the collected subframes as one A-MSDU MPDU
 * (802.11 header of the first, then every subframe padded to 4 bytes but
 * the last), as it was sent over the air. One subframe goes as it is.
 */
static void
mwx_amsdu_flush(struct mwx_softc *sc, struct mbuf_list *ml)
{
	int n = sc->sc_amsdu_n, i;
	u_int hl = sc->sc_amsdu_hdrlen;
	size_t total, off;
	struct mbuf *nm = NULL;
	u_int32_t chunks = 1;
	uint8_t *dst;

	if (n == 0)
		return;
	sc->sc_amsdu_n = 0;
	if (n == 1) {
		gYYAmsduSingle++;
		mwx_rx_deliver(sc, sc->sc_amsdu_sub[0], &sc->sc_amsdu_rxi, ml);
		sc->sc_amsdu_sub[0] = NULL;
		return;
	}
	total = hl;
	for (i = 0; i < n; i++)
		total += i < n - 1 ? ((sc->sc_amsdu_len[i] + 3) & ~3U) : sc->sc_amsdu_len[i];
	if (mbuf_allocpacket(MBUF_DONTWAIT, total, &chunks, &nm) != 0 || nm == NULL ||
	    mbuf_next(nm) != NULL) {	/* chunks = 1: one buffer of >= total bytes */
		if (nm != NULL)
			m_freem(nm);
		gYYAmsduAllocFail++;
		for (i = 0; i < n; i++) {
			m_freem(sc->sc_amsdu_sub[i]);
			sc->sc_amsdu_sub[i] = NULL;
		}
		return;
	}
	mbuf_setlen(nm, total);
	mbuf_pkthdr_setlen(nm, total);
	dst = mtod(nm, uint8_t *);
	memcpy(dst, mtod(sc->sc_amsdu_sub[0], uint8_t *), hl);
	off = hl;
	for (i = 0; i < n; i++) {
		struct mbuf *sm = sc->sc_amsdu_sub[i];
		u_int len = sc->sc_amsdu_len[i];

		mbuf_copydata(sm, hl, len, dst + off);
		off += len;
		if (i < n - 1) {
			u_int pad = ((len + 3) & ~3U) - len;
			memset(dst + off, 0, pad);
			off += pad;
		}
		m_freem(sm);
		sc->sc_amsdu_sub[i] = NULL;
	}
	gYYAmsduMpdu++;
	mwx_rx_deliver(sc, nm, &sc->sc_amsdu_rxi, ml);
}

/* YYavic RX A-MSDU: collect one hardware-split subframe (see fill_rx). */
static void
mwx_amsdu_add(struct mwx_softc *sc, struct mbuf *m, struct ieee80211_rxinfo *rxi,
    struct mbuf_list *ml)
{
	struct ieee80211_frame *wh = mtod(m, struct ieee80211_frame *);
	int fmt = (rxi->rxi_flags & MWX_RXI_AMSDU_MASK) >> MWX_RXI_AMSDU_SHIFT;
	uint16_t seq = letoh16(*(uint16_t *)wh->i_seq) >> IEEE80211_SEQ_SEQ_SHIFT;
	u_int hl = ieee80211_get_hdrlen(wh);

	rxi->rxi_flags &= ~MWX_RXI_AMSDU_MASK;
	if (sc->sc_amsdu_n > 0 &&
	    (fmt == MT_RXD4_FIRST_AMSDU_FRAME || seq != sc->sc_amsdu_seq || hl != sc->sc_amsdu_hdrlen ||
	     !IEEE80211_ADDR_EQ(wh->i_addr2, sc->sc_amsdu_ta))) {
		gYYAmsduFlushSeq++;
		mwx_amsdu_flush(sc, ml);
	}
	if (sc->sc_amsdu_n == MWX_AMSDU_MAX_SUB) {
		gYYAmsduFlushFull++;
		mwx_amsdu_flush(sc, ml);
	}
	sc->sc_amsdu_idle = 0;
	if (sc->sc_amsdu_n == 0) {
		sc->sc_amsdu_seq = seq;
		sc->sc_amsdu_hdrlen = hl;
		IEEE80211_ADDR_COPY(sc->sc_amsdu_ta, wh->i_addr2);
		sc->sc_amsdu_rxi = *rxi;
	}
	sc->sc_amsdu_sub[sc->sc_amsdu_n] = m;
	sc->sc_amsdu_len[sc->sc_amsdu_n] = (uint16_t)(mbuf_len(m) - hl);	/* one RX buffer */
	sc->sc_amsdu_n++;
	if ((uint32_t)sc->sc_amsdu_n > gYYAmsduMaxN)
		gYYAmsduMaxN = sc->sc_amsdu_n;
	if (fmt == MT_RXD4_LAST_AMSDU_FRAME)
		mwx_amsdu_flush(sc, ml);
}

void
mwx_rx(struct mwx_softc *sc, struct mbuf *m, struct mbuf_list *ml)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct ieee80211_frame *wh;
	struct ieee80211_rxinfo rxi = { 0 };
	int rv;

	{
		static uint32_t drxd;
		if (ic->ic_state >= IEEE80211_S_ASSOC && drxd < 24 && mbuf_len(m) >= 48) {
			uint32_t *w = mtod(m, uint32_t *);
			drxd++;
			MWX_DEV_LOG(sc, "DIAG RXD state=%d len=%zu: %08x %08x %08x %08x %08x %08x %08x %08x | %08x %08x %08x %08x\n",
			    ic->ic_state, mbuf_len(m), le32toh(w[0]), le32toh(w[1]), le32toh(w[2]), le32toh(w[3]),
			    le32toh(w[4]), le32toh(w[5]), le32toh(w[6]), le32toh(w[7]), le32toh(w[8]), le32toh(w[9]),
			    le32toh(w[10]), le32toh(w[11]));
		}
	}
	rv = mt7921_mac_fill_rx(sc, m, &rxi);
	if (rv != 0) {
		if (sc->sc_rx_fill_fail < 32) {
			MWX_DEV_LOG(sc,
			    "rx fill failed rv=%d len=%zu fail_count=%u\n",
			    rv, mbuf_len(m), sc->sc_rx_fill_fail + 1);
		}
		sc->sc_rx_fill_fail++;
		ifp->if_ierrors++;
		m_freem(m);
		return;
	}
	if (rxi.rxi_flags & MWX_RXI_AMSDU_MASK) {	/* YYavic: A-MSDU subframe */
		mwx_amsdu_add(sc, m, &rxi, ml);
		return;
	}
	if (sc->sc_amsdu_n > 0)		/* another frame ends a pending A-MSDU */
		mwx_amsdu_flush(sc, ml);

	wh = mtod(m, struct ieee80211_frame *);
	if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) == IEEE80211_FC0_TYPE_DATA) {
		gRxData++;
		if (wh->i_fc[1] & IEEE80211_FC1_RETRY)
			gRxRetry++;
	}
	{
		static uint32_t drx;
		if (ic->ic_state >= IEEE80211_S_AUTH && drx < 80 &&
		    (wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) != IEEE80211_FC0_TYPE_DATA) {
			drx++;
			MWX_DEV_LOG(sc, "DIAG RX state=%d type=0x%02x subtype=0x%02x len=%zu rssi=%d\n", ic->ic_state,
			    wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK, wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK, mbuf_len(m), rxi.rxi_rssi);
			if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) == IEEE80211_FC0_TYPE_MGT &&
			    (wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) != IEEE80211_FC0_SUBTYPE_BEACON)
				mwx_diag_hex(sc, "DIAG RX", (const uint8_t *)wh, mbuf_len(m), 96);
		}
	}
	if (sc->sc_rx_delivered < 32) {
		uint8_t type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
		uint8_t subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;

		MWX_DEV_LOG(sc,
		    "rx frame delivered type=0x%02x subtype=0x%02x chan=%u rssi=%d len=%zu\n",
		    type, subtype, rxi.rxi_chan, rxi.rxi_rssi, mbuf_len(m));
	}
	{
		static uint32_t rx_run_log_count;
		uint8_t type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
		uint8_t subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;

		if (rx_run_log_count < 48 &&
		    (type == IEEE80211_FC0_TYPE_DATA ||
		    (ic->ic_state == IEEE80211_S_RUN &&
		    (rxi.rxi_flags & IEEE80211_RXI_HWDEC)))) {
			MWX_DEV_LOG(sc,
			    "rx run frame state=%d fc=%02x/%02x type=0x%02x subtype=0x%02x rxi_flags=0x%x chan=%u rssi=%d len=%zu\n",
			    ic->ic_state, wh->i_fc[0], wh->i_fc[1], type, subtype,
			    rxi.rxi_flags, rxi.rxi_chan, rxi.rxi_rssi, mbuf_len(m));
			rx_run_log_count++;
		}
	}
	sc->sc_rx_delivered++;

#if NBPFILTER > 0
	if (__predict_false(sc->sc_drvbpf != NULL)) {
		struct mwx_rx_radiotap_header *tap = &sc->sc_rxtap;
		uint32_t tsf_lo, tsf_hi;
		/* get timestamp (low and high 32 bits) */
		tsf_hi = 0;
		tsf_lo = 0;
		tap->wr_tsft = htole64(((uint64_t)tsf_hi << 32) | tsf_lo);
		tap->wr_flags = 0;
		tap->wr_rate = 2;	/* XXX */
		tap->wr_chan_freq =
		    htole16(ic->ic_channels[rxi.rxi_chan].ic_freq);
		tap->wr_chan_flags =
		    ic->ic_channels[rxi.rxi_chan].ic_flags;
		tap->wr_dbm_antsignal = rxi.rxi_rssi;
		bpf_mtap_hdr(sc->sc_drvbpf, tap, sc->sc_rxtap_len, m,
		    BPF_DIRECTION_IN);
	}
#endif

	mwx_rx_deliver(sc, m, &rxi, ml);
}

/*
 * R9T trace32: RX Block Ack (A-MPDU alma). Linux mt7921_mcu_uni_rx_ba ->
 * mt76_connac_mcu_sta_ba(..., STA_REC_UPDATE, enable, tx=false). FW komutu
 * beklemeli oldugu icin RX yolunda degil gorevde gonderilir; net80211'e EBUSY
 * donulur, sonuc ieee80211_addba_req_accept/refuse ile bildirilir.
 */
static int
mwx_ba_enqueue(struct mwx_softc *sc, struct ieee80211_node *ni, uint8_t tid, int start, int tx)
{
	int next = (sc->sc_ba_cur + 1) % nitems(sc->sc_ba_arg);

	if (next == sc->sc_ba_tail)
		return ENOBUFS;
	sc->sc_ba_arg[sc->sc_ba_cur].ni = ieee80211_ref_node(ni);
	sc->sc_ba_arg[sc->sc_ba_cur].tid = tid;
	sc->sc_ba_arg[sc->sc_ba_cur].start = start;
	sc->sc_ba_arg[sc->sc_ba_cur].tx = tx;
	sc->sc_ba_cur = next;
	task_add(sc->sc_nswq, &sc->sc_ba_task);
	return 0;
}

static int
mwx_ampdu_rx_start(struct ieee80211com *ic, struct ieee80211_node *ni, uint8_t tid)
{
	struct mwx_softc *sc = (struct mwx_softc *)ic->ic_softc;

	if (mwx_ba_enqueue(sc, ni, tid, 1, 0) != 0)
		return ENOBUFS;
	return EBUSY;	/* gorev kabul/ret edecek */
}

static void
mwx_ampdu_rx_stop(struct ieee80211com *ic, struct ieee80211_node *ni, uint8_t tid)
{
	struct mwx_softc *sc = (struct mwx_softc *)ic->ic_softc;

	(void)mwx_ba_enqueue(sc, ni, tid, 0, 0);
}

/* R9T trace37: TX Block Ack (originator). Linux mt7921_mcu_uni_tx_ba. */
static int
mwx_ampdu_tx_start(struct ieee80211com *ic, struct ieee80211_node *ni, uint8_t tid)
{
	struct mwx_softc *sc = (struct mwx_softc *)ic->ic_softc;

	if (mwx_ba_enqueue(sc, ni, tid, 1, 1) != 0)
		return ENOBUFS;
	return EBUSY;
}

static void
mwx_ampdu_tx_stop(struct ieee80211com *ic, struct ieee80211_node *ni, uint8_t tid)
{
	struct mwx_softc *sc = (struct mwx_softc *)ic->ic_softc;

	(void)mwx_ba_enqueue(sc, ni, tid, 0, 1);
}

static int
mwx_mcu_sta_ba(struct mwx_softc *sc, struct ieee80211_node *ni, uint8_t tid, int enable, int tx)
{
	struct mwx_node *mw = (struct mwx_node *)ni;
	struct mwx_vif *mvif = &sc->sc_vif;
	struct sta_rec_ba *rba;
	uint16_t ssn, win, params;
	struct mbuf *m;
	uint16_t tlvnum = 0;

	/*
	 * R9T trace38: Linux mt76_connac_mcu_sta_ba once STA_REC_WTBL + WTBL_BA (donanim BA
	 * tablosu: tid/sn/pencere/etkin) gonderir, sonra STA_REC_BA. TX toplama icin gerekli.
	 */
	{
		struct sta_rec_wtbl *wtbl;
		struct wtbl_ba *wba;
		uint16_t wnum = 0, wtlv = 0;
		int wrv;

		m = mwx_alloc_sta_req_tlv(sizeof(struct sta_req_hdr));
		if (m == NULL)
			return ENOBUFS;
		wtbl = (struct sta_rec_wtbl *)mwx_append_tlv(m, &wtlv, STA_REC_WTBL, sizeof(*wtbl));
		wtbl->wlan_idx_lo = mw->wcid & 0xff;
		wtbl->wlan_idx_hi = mw->wcid >> 8;
		wtbl->operation = WTBL_SET;
		wba = (struct wtbl_ba *)mwx_append_tlv(m, &wnum, WTBL_BA, sizeof(*wba));
		wba->tid = tid;
		if (tx) {
			wba->ba_type = MT_BA_TYPE_ORIGINATOR;
			wba->sn = enable ? htole16(ni->ni_tx_ba[tid].ba_winstart) : 0;
			wba->ba_en = enable ? 1 : 0;
			wba->ba_winsize = enable ? htole16(ni->ni_tx_ba[tid].ba_winsize) : 0;
		} else {
			memcpy(wba->peer_addr, ni->ni_macaddr, IEEE80211_ADDR_LEN);
			wba->ba_type = MT_BA_TYPE_RECIPIENT;
			wba->rst_ba_tid = tid;
			wba->rst_ba_sel = 0;	/* RST_BA_MAC_TID_MATCH */
			wba->rst_ba_sb = 1;
			wba->ba_winsize = enable ? htole16(ni->ni_rx_ba[tid].ba_winsize) : 0;
		}
		wtbl->tlv_num = htole16(wnum);
		wtbl->len = htole16(le16toh(wtbl->len) + sizeof(*wba));
		m = mwx_fill_sta_req_hdr(m, mvif, mvif->omac_idx, mw->wcid, wtlv);
		if (m == NULL)
			return ENOBUFS;
		wrv = mwx_mcu_send_mbuf_wait(sc, MCU_UNI_CMD_STA_REC_UPDATE, m);
		{
			static uint32_t n;
			if (n++ < 16)
				smtw_trace("mwx: WTBL_BA %s tid=%u en=%d rv=%d\n", tx ? "tx" : "rx", tid, enable, wrv);
		}
		if (wrv != 0)
			return wrv;
	}

	m = mwx_alloc_sta_req_tlv(sizeof(struct sta_req_hdr));
	if (m == NULL)
		return ENOBUFS;
	rba = (struct sta_rec_ba *)mwx_append_tlv(m, &tlvnum, STA_REC_BA, sizeof(*rba));
	if (tx) {
		ssn = ni->ni_tx_ba[tid].ba_winstart;
		win = ni->ni_tx_ba[tid].ba_winsize;
		params = ni->ni_tx_ba[tid].ba_params;
	} else {
		ssn = ni->ni_rx_ba[tid].ba_winstart;
		win = ni->ni_rx_ba[tid].ba_winsize;
		params = ni->ni_rx_ba[tid].ba_params;
	}
	rba->tid = tid;
	rba->ba_type = tx ? MT_BA_TYPE_ORIGINATOR : MT_BA_TYPE_RECIPIENT;
	rba->amsdu = (params & IEEE80211_ADDBA_AMSDU) != 0;
	rba->ba_en = enable ? (1U << tid) : 0;
	rba->ssn = htole16(ssn);
	rba->winsize = htole16(win);
	m = mwx_fill_sta_req_hdr(m, mvif, mvif->omac_idx, mw->wcid, tlvnum);
	if (m == NULL)
		return ENOBUFS;
	return mwx_mcu_send_mbuf_wait(sc, MCU_UNI_CMD_STA_REC_UPDATE, m);
}

static void
mwx_ba_task(void *arg)
{
	struct mwx_softc *sc = (struct mwx_softc *)arg;
	struct ieee80211com *ic = &sc->sc_ic;
	int s = splnet();

	while (sc->sc_ba_tail != sc->sc_ba_cur) {
		struct ieee80211_node *ni = sc->sc_ba_arg[sc->sc_ba_tail].ni;
		uint8_t tid = sc->sc_ba_arg[sc->sc_ba_tail].tid;
		int start = sc->sc_ba_arg[sc->sc_ba_tail].start;
		int tx = sc->sc_ba_arg[sc->sc_ba_tail].tx;
		int rv;

		sc->sc_ba_arg[sc->sc_ba_tail].ni = NULL;
		sc->sc_ba_tail = (sc->sc_ba_tail + 1) % nitems(sc->sc_ba_arg);

		if (ic->ic_state != IEEE80211_S_RUN || ni != ic->ic_bss) {
			/* baglanti degisti; istek gecersiz */
			if (start && !tx)
				ieee80211_addba_req_refuse(ic, ni, tid);
			else if (start && tx)
				ieee80211_addba_resp_refuse(ic, ni, tid, IEEE80211_STATUS_UNSPECIFIED);
			ieee80211_release_node(ic, ni);
			continue;
		}
		rv = mwx_mcu_sta_ba(sc, ni, tid, start, tx);
		{
			static uint32_t n;
			if (n++ < 16)
				smtw_trace("mwx: %s BA %s tid=%u ssn=%u win=%u rv=%d\n", tx ? "TX" : "RX", start ? "start" : "stop",
				    tid, tx ? ni->ni_tx_ba[tid].ba_winstart : ni->ni_rx_ba[tid].ba_winstart,
				    tx ? ni->ni_tx_ba[tid].ba_winsize : ni->ni_rx_ba[tid].ba_winsize, rv);
		}
		if (start && !tx) {
			if (rv == 0)
				ieee80211_addba_req_accept(ic, ni, tid);
			else
				ieee80211_addba_req_refuse(ic, ni, tid);
		} else if (start && tx) {
			if (rv == 0)
				ieee80211_addba_resp_accept(ic, ni, tid);
			else
				ieee80211_addba_resp_refuse(ic, ni, tid, IEEE80211_STATUS_UNSPECIFIED);
		}
		ieee80211_release_node(ic, ni);
	}
	splx(s);
}

int
mwx_count_nodes(struct ieee80211com *ic)
{
	struct ieee80211_node *ni;
	int count = 0;

	RB_FOREACH(ni, ieee80211_tree, &ic->ic_tree)
		count++;
	return count;
}

/*
 * Driver specific functions.
 */
int
mwx_intr_body(void *arg)
{
	struct mwx_softc *sc = (struct mwx_softc *)arg;
	uint32_t intr, intr_sw;
	uint32_t mask = sc->sc_intr_mask;
	static uint32_t intr_log_count;
	static uint32_t intr_unhandled_log_count;

	mwx_write(sc, MT_WFDMA0_HOST_INT_ENA, 0);
	intr = mwx_read(sc, MT_WFDMA0_HOST_INT_STA);
	if (intr == 0) {
		mwx_write(sc, MT_WFDMA0_HOST_INT_ENA, mask);
		return 0;
	}

	/* TODO power management */
//	mt76_connac_pm_ref(&dev->mphy, &dev->pm);

	if (intr_log_count < 16) {
		MWX_DEV_LOG(sc, "interrupt status=0x%08x mask=0x%08x\n",
		    intr, mask);
		intr_log_count++;
	}

	if (intr & ~mask && intr_unhandled_log_count < 16) {
		smtw_trace("%s: unhandled interrupt %08x\n", DEVNAME(sc),
		    intr & ~mask);
		intr_unhandled_log_count++;
	}
	/* ack interrupts */
	intr &= mask;
	mwx_write(sc, MT_WFDMA0_HOST_INT_STA, intr);
	{
		clock_sec_t hs; clock_usec_t hu; clock_get_system_microtime(&hs, &hu);
		uint32_t hi = gMwxHistN++ & 15;
		gMwxHist[hi][0] = (uint32_t)(hs * 1000 + hu / 1000); gMwxHist[hi][1] = intr; gMwxHist[hi][2] = 0;
	}

	if (intr & MT_INT_MCU_CMD) {
		intr_sw = mwx_read(sc, MT_MCU_CMD);
		gMwxHist[(gMwxHistN - 1) & 15][2] = intr_sw;
		if (intr_sw & ~MT_MCU_CMD_WAKE_RX_PCIE)
			MWX_DEV_LOG(sc, "MCU_CMD sw_int=0x%08x\n", intr_sw);
		if (intr_log_count < 16)
			MWX_DEV_LOG(sc, "mcu interrupt status=0x%08x\n", intr_sw);
		/* ack MCU2HOST_SW_INT_STA */
		mwx_write(sc, MT_MCU_CMD, intr_sw);
		if (intr_sw & MT_MCU_CMD_WAKE_RX_PCIE)
			intr |= MT_INT_RX_DONE_DATA;
	}

	if (intr & MT_INT_TX_DONE_ALL)
		mwx_dma_tx_done(sc);

	if (intr & MT_INT_RX_DONE_WM)
		mwx_dma_rx_done(sc, &sc->sc_rxfwdlq);
	if (intr & MT_INT_RX_DONE_WM2)
		mwx_dma_rx_done(sc, &sc->sc_rxmcuq);
	if (intr & MT_INT_RX_DONE_DATA)
		mwx_dma_rx_done(sc, &sc->sc_rxq);

	/* TODO power management */
//	mt76_connac_pm_unref(&dev->mphy, &dev->pm);

	mwx_write(sc, MT_WFDMA0_HOST_INT_ENA, mask);

	return 1;
}

int
mwx_intr(void *arg)
{
	int rv;

	gMwxIntrCalls++;
	if (!OSCompareAndSwap(0, 1, &gMwxIntrBusy)) {
		gMwxCasFails++;
		return 0;
	}
	gMwxBodyCalls++;
	uint64_t t0 = mach_absolute_time();
	rv = mwx_intr_body(arg);
	gYYPfIntr += mach_absolute_time() - t0;
	gYYPfIntrN++;
	OSCompareAndSwap(1, 0, &gMwxIntrBusy);
	return rv;
}

int
mwx_preinit(struct mwx_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	int rv, i;
	uint8_t chan;

	MWX_DEV_LOG(sc, "preinit start fw_loaded=%d\n", sc->sc_fw_loaded);

	/*
	 * R9T: firmware indirme aralikli olarak (yaklasik 2 acilistan 1'i) cevapsiz kaliyor; her deneme
	 * mwx_dma_reset(fullreset) ile tam WFSYS reset yapiyor, bu yuzden bastan denemek guvenli.
	 */
	{
		int attempt;
		for (attempt = 1; attempt <= 4; attempt++) {
			rv = mwx_init_hardware(sc);
			if (rv == 0)
				break;
			MWX_DEV_LOG(sc, "preinit hardware init failed error=%d (deneme %d/4)\n", rv, attempt);
			if (attempt < 4)
				IOSleep(300);
		}
	}
	if (rv != 0) {
		MWX_DEV_LOG(sc, "preinit hardware init failed error=%d\n", rv);
		return rv;
	}
	MWX_DEV_LOG(sc, "preinit hardware init ok\n");

	if ((rv = mwx_mcu_set_deep_sleep(sc, 1)) != 0) {
		MWX_DEV_LOG(sc, "preinit deep sleep enable failed error=%d\n", rv);
		return rv;
	}
	MWX_DEV_LOG(sc, "preinit deep sleep enabled\n");

	ic->ic_sup_rates[IEEE80211_MODE_11B] = ieee80211_std_rateset_11b;
	ic->ic_sup_rates[IEEE80211_MODE_11G] = ieee80211_std_rateset_11g;
	MWX_DEV_LOG(sc, "preinit capability 2ghz=%d 5ghz=%d 6ghz=%d streams=%u antenna_mask=0x%x\n",
	    sc->sc_capa.has_2ghz, sc->sc_capa.has_5ghz,
	    sc->sc_capa.has_6ghz, sc->sc_capa.num_streams,
	    sc->sc_capa.antenna_mask);

	if (sc->sc_capa.has_5ghz)
		ic->ic_sup_rates[IEEE80211_MODE_11A] =
		    ieee80211_std_rateset_11a;
	/* 0.7.1: only the channels of the selected country (mwx_reg_*) */
	mwx_reg_channels(sc);
#ifdef NOTYET
	/* TODO support for 6GHz */
	if (sc->sc_capa.has_6ghz) {
		for (i = 0; i < MWX_NUM_6GHZ_CHANNELS; i++) {
		}
	}
#endif

	/* R9T trace27: desteklenen HT MCS (1-2 akis) */
	memset(ic->ic_sup_mcs, 0, sizeof(ic->ic_sup_mcs));
	ic->ic_sup_mcs[0] = 0xff;
	if (sc->sc_capa.num_streams > 1)
		ic->ic_sup_mcs[1] = 0xff;
	ic->ic_tx_mcs_set = IEEE80211_TX_MCS_SET_DEFINED;
	{
		/* YYavic HE: HE-MCS 0-11 for each stream we have, 3 (none) for the rest */
		uint16_t map = 0xffff;
		int st, nss = sc->sc_capa.num_streams > 1 ? 2 : 1;
		for (st = 0; st < nss; st++)
			map &= ~(uint16_t)((3 ^ IEEE80211_HE_MCS_SUPPORT_0_11) << (2 * st));
		ic->ic_he_mcs_nss_supp.rx_mcs_80 = htole16(map);
		ic->ic_he_mcs_nss_supp.tx_mcs_80 = htole16(map);
		ic->ic_he_mcs_nss_supp.rx_mcs_160 = htole16(map);	/* 0.6.7 */
		ic->ic_he_mcs_nss_supp.tx_mcs_160 = htole16(map);
	}

	/* Configure channel information obtained from firmware. */
	ieee80211_channel_init(ifp);
	MWX_DEV_LOG(sc, "preinit channel table ready\n");

	if (IEEE80211_ADDR_EQ(etheranyaddr, sc->sc_ic.ic_myaddr))
		IEEE80211_ADDR_COPY(ic->ic_myaddr, sc->sc_lladdr);

	/* Configure MAC address. */
	rv = if_setlladdr(ifp, ic->ic_myaddr);
	if (rv)
		smtw_trace("%s: could not set MAC address (error %d)\n",
		    DEVNAME(sc), rv);
	else
		MWX_DEV_LOG(sc, "preinit mac address=%s\n",
		    ether_sprintf(ic->ic_myaddr));

	ieee80211_media_init(ifp);
	/* R9T trace27: HT acik, QoS veri cerceveleri */
	if (ic->ic_modecaps & (1 << IEEE80211_MODE_11N))
		ic->ic_flags |= IEEE80211_F_HTON;
	if (ic->ic_modecaps & (1 << IEEE80211_MODE_11AC))
		ic->ic_flags |= IEEE80211_F_VHTON;	/* R9T trace33 */
	if ((ic->ic_modecaps & (1 << IEEE80211_MODE_11AX)) && !gYYNoHe)
		ic->ic_flags |= IEEE80211_F_HEON;	/* YYavic HE; off: -yyavicnohe / dbg 33 */
	ic->ic_flags |= IEEE80211_F_QOS;
	MWX_DEV_LOG(sc, "preinit HT modecaps=0x%x flags=0x%x htcaps=0x%x mcs=%02x%02x\n",
	    ic->ic_modecaps, ic->ic_flags, ic->ic_htcaps, ic->ic_sup_mcs[0], ic->ic_sup_mcs[1]);

	sc->sc_fw_loaded = 1;
	MWX_DEV_LOG(sc, "preinit complete\n");
	return 0;
}

void
mwx_attach_hook(struct device *self)
{
	struct mwx_softc *sc = (struct mwx_softc *)self;

	MWX_DEV_LOG(sc, "attach hook start\n");
	mwx_preinit(sc);
	MWX_DEV_LOG(sc, "attach hook done fw_loaded=%d\n", sc->sc_fw_loaded);
}

bool
mwx_attach(struct device *parent, struct device *self, void *aux)
{
	struct mwx_softc *sc = (struct mwx_softc *)self;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;
	pci_intr_handle_t ih;
	pcireg_t memtype;
	uint32_t hwid, hwrev;
	int error;

	MWX_DEV_LOG(sc, "attach start pci_id=0x%08x vendor=0x%04x product=0x%04x\n",
	    pa->pa_id, PCI_VENDOR(pa->pa_id), PCI_PRODUCT(pa->pa_id));
	sc->sc_pc = pa->pa_pc;
	sc->sc_tag = pa->pa_tag;
	sc->sc_dmat = pa->pa_dmat;
	sc->sc_intr_mask = MT_INT_RX_DONE_ALL | MT_INT_TX_DONE_ALL |
	    MT_INT_MCU_CMD;
	if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_MEDIATEK_MT7922 ||
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_MEDIATEK_RZ616)
		sc->sc_hwtype = MWX_HW_MT7922;
	else
		sc->sc_hwtype = MWX_HW_MT7921;
	MWX_DEV_LOG(sc, "attach hardware type=%s\n",
	    sc->sc_hwtype == MWX_HW_MT7922 ? "MT7922" : "MT7921");

	memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, PCI_MAPREG_START);
	if (pci_mapreg_map(pa, PCI_MAPREG_START, memtype, 0,
	    &sc->sc_st, &sc->sc_memh, NULL, &sc->sc_mems, 0)) {
		smtw_trace("%s: can't map mem space\n", DEVNAME(sc));
		return false;
	}
	MWX_DEV_LOG(sc, "attach BAR mapped memtype=0x%x size=%zu\n",
	    memtype, (size_t)sc->sc_mems);

	if (pci_intr_map_msix(pa, 0, &ih) &&
	    pci_intr_map_msi(pa, &ih) &&
	    pci_intr_map(pa, &ih)) {
		smtw_trace("%s: can't map interrupt\n", DEVNAME(sc));
		/* HAL detach owns the mapping on failure. */
		return false;
	}
	MWX_DEV_LOG(sc, "attach interrupt mapped source=%s\n",
	    pci_intr_string(pa->pa_pc, ih));

	if (mwx_mcu_fw_pmctrl(sc) != 0 ||
	    mwx_mcu_drv_pmctrl(sc) != 0) {
		MWX_DEV_LOG(sc, "attach MCU power control failed\n");
		{
			uint32_t o[] = { 0x0, 0x4, 0x10194, 0xd4200, 0xd4208, 0xe0010, 0xe00f0 };
			unsigned k;
			for (k = 0; k < sizeof(o) / sizeof(o[0]); k++)
				MWX_DEV_LOG(sc, "DIAG bar[0x%05x]=0x%08x\n", o[k],
				    bus_space_read_4(sc->sc_st, sc->sc_memh, o[k]));
			MWX_DEV_LOG(sc, "DIAG chipid=0x%08x rev=0x%08x\n",
			    mwx_read(sc, MT_HW_CHIPID), mwx_read(sc, MT_HW_REV));
			MWX_DEV_LOG(sc, "DIAG wfsys_rst=0x%08x top_lpcr=0x%08x conn_misc=0x%08x\n",
			    mwx_read(sc, MT_WFSYS_SW_RST_B), mwx_read(sc, MT_TOP_LPCR_HOST_BAND0),
			    mwx_read(sc, MT_CONN_ON_MISC));
		}
		goto fail;
	}
	MWX_DEV_LOG(sc, "attach MCU power control ok\n");

	hwid = mwx_read(sc, MT_HW_CHIPID) & 0xffff;
	hwrev = mwx_read(sc, MT_HW_REV) & 0xff;

	MWX_DEV_LOG(sc, "attach chip id=0x%04x hwrev=0x%02x interrupt=%s\n",
	    hwid, hwrev, pci_intr_string(pa->pa_pc, ih));
	/* The chip id decides, as Linux is_mt7922() (mt76_chip == 0x7922);
	 * the PCI product id only picks the default. */
	if (hwid == 0x7922 && sc->sc_hwtype != MWX_HW_MT7922) {
		MWX_DEV_LOG(sc, "attach chip is MT7922 (pci product 0x%04x)\n",
		    PCI_PRODUCT(pa->pa_id));
		sc->sc_hwtype = MWX_HW_MT7922;
	}

	mwx_set(sc, MT_MCU2HOST_SW_INT_ENA, MT_MCU_CMD_WAKE_RX_PCIE);
	if (mwx_wfsys_reset(sc) != 0) {
		MWX_DEV_LOG(sc, "attach WFSYS reset failed\n");
		goto fail;
	}
	MWX_DEV_LOG(sc, "attach WFSYS reset ok\n");

	mwx_write(sc, MT_WFDMA0_HOST_INT_ENA, 0);
	mwx_write(sc, MT_PCIE_MAC_INT_ENABLE, 0xff);

	sc->sc_ih = pci_intr_establish(pa->pa_pc, ih, IPL_NET,
	    mwx_intr, sc, DEVNAME(sc));
	if (sc->sc_ih == NULL) {
		smtw_trace("%s: can't establish interrupt\n", DEVNAME(sc));
		goto fail;
	}
	MWX_DEV_LOG(sc, "attach interrupt established handle=%p\n",
	    (void *)sc->sc_ih);

	if ((error = mwx_txwi_alloc(sc, MWX_TXWI_MAX)) != 0) {
		smtw_trace("%s: failed to allocate DMA resources %d\n",
		    DEVNAME(sc), error);
		goto fail;
	}

	if ((error = mwx_dma_alloc(sc)) != 0) {
		smtw_trace("%s: failed to allocate DMA resources %d\n",
		    DEVNAME(sc), error);
		goto fail;
	}
	MWX_DEV_LOG(sc, "attach DMA resources ready\n");

	/* regulatory domain: boot-arg yyavic-cc=XX, else '00' (0.7.1) */
	{
		char cc[4] = { 0 };

		/* "00" parses as the number 0: cc stays empty -> '00' */
		if (!PE_parse_boot_argn("yyavic-cc", cc, sizeof(cc)))
			cc[0] = '\0';
		cc[sizeof(cc) - 1] = '\0';
		mwx_reg_set(sc, cc);
	}

	ic->ic_phytype = IEEE80211_T_OFDM;	/* not only, but not used */
	ic->ic_opmode = IEEE80211_M_STA;	/* default to BSS mode */
	ic->ic_state = IEEE80211_S_INIT;

	/* Set device capabilities. */
	ic->ic_caps =
	    IEEE80211_C_AMSDU_IN_AMPDU |	/* YYavic: RX A-MSDU in A-MPDU (ADDBA) */
	    IEEE80211_C_QOS |		/* R9T trace27: WMM/QoS (HT icin) */
	    IEEE80211_C_TX_AMPDU |	/* R9T trace37: TX A-MPDU (ADDBA net80211, BA FW) */
#if NOTYET
	    IEEE80211_C_TX_AMPDU | /* A-MPDU */
	    IEEE80211_C_ADDBA_OFFLOAD | /* device sends ADDBA/DELBA frames */
#endif
	    IEEE80211_C_WEP |		/* WEP */
	    IEEE80211_C_RSN |		/* WPA/RSN */
	    IEEE80211_C_SCANALL |	/* device scans all channels at once */
	    IEEE80211_C_SCANALLBAND |	/* device scans all bands at once */
	    IEEE80211_C_MONITOR |	/* monitor mode supported */
#ifndef IEEE80211_STA_ONLY
	    IEEE80211_C_IBSS |		/* IBSS mode supported */
	    IEEE80211_C_HOSTAP |	/* HostAP mode supported */
	    IEEE80211_C_APPMGT |	/* HostAP power management */
#endif
	    IEEE80211_C_SHSLOT |	/* short slot time supported */
	    IEEE80211_C_SHPREAMBLE;	/* short preamble supported */

#if NOTYET
	ic->ic_htcaps = IEEE80211_HTCAP_SGI20 | IEEE80211_HTCAP_SGI40;
	ic->ic_htcaps |= IEEE80211_HTCAP_CBW20_40;
	ic->ic_htcaps |=
	    (IEEE80211_HTCAP_SMPS_DIS << IEEE80211_HTCAP_SMPS_SHIFT);
#endif
	/* R9T trace27: HT20, SGI20, SMPS kapali (40 MHz / VHT sonraki asama) */
	ic->ic_htcaps = IEEE80211_HTCAP_SGI20 | IEEE80211_HTCAP_SGI40 |
	    IEEE80211_HTCAP_CBW20_40 |		/* R9T trace33: 40 MHz */
	    IEEE80211_HTCAP_LDPC |		/* YYavic: as Linux mt7921 */
	    IEEE80211_HTCAP_AMSDU7935 |
	    (1 << IEEE80211_HTCAP_RXSTBC_SHIFT) |
	    (IEEE80211_HTCAP_SMPS_DIS << IEEE80211_HTCAP_SMPS_SHIFT);
	/* R9T trace33: VHT (11ac) 80 MHz, 2 akis MCS0-9 */
	/* YYavic: Linux mt7921 VHT set (11454 MPDU, 1 MB A-MPDU, LDPC, RX
	 * STBC, SU/MU beamformee, 4 STS). Large A-MSDUs are joined back from
	 * the hardware-split subframes in mwx_amsdu_add/flush. */
	ic->ic_vhtcaps = IEEE80211_VHTCAP_MAX_MPDU_LENGTH_11454 |
	    IEEE80211_VHTCAP_SHORT_GI_80 |
	    IEEE80211_VHTCAP_SUPP_CHAN_WIDTH_160MHZ | IEEE80211_VHTCAP_SHORT_GI_160 |	/* 160 MHz (MT7922, Linux); off: dbg 35 a=10 */
	    IEEE80211_VHTCAP_RXLDPC | IEEE80211_VHTCAP_RXSTBC_1 |
	    IEEE80211_VHTCAP_SU_BEAMFORMEE_CAPABLE | IEEE80211_VHTCAP_MU_BEAMFORMEE_CAPABLE |
	    (3 << IEEE80211_VHTCAP_BEAMFORMEE_STS_SHIFT) |
	    (IEEE80211_VHTCAP_MAX_AMPDU_1024K << IEEE80211_VHTCAP_MAX_A_MPDU_LENGTH_EXPONENT_SHIFT);
	ic->ic_vht_rx_mcs_map = 0xfffa;	/* NSS1/2: MCS0-9, digerleri yok */
	ic->ic_vht_tx_mcs_map = 0xfffa;
	ic->ic_vht_rx_highest = 0;
	ic->ic_vht_tx_highest = 0;
	ic->ic_htxcaps = 0;
	ic->ic_txbfcaps = 0;
	ic->ic_aselcaps = 0;
	ic->ic_ampdu_params = (IEEE80211_AMPDU_PARAM_SS_4 | 0x3 /* 64k */);

	/* YYavic HE (802.11ax): station capabilities as in Linux
	 * mt7921_init_he_caps() for the 5 GHz band, 2 streams HE-MCS 0-11.
	 * 80 MHz only for now (no 160 MHz bit: VHT caps are 80 MHz, too), and
	 * no PPE thresholds: nominal packet padding 16 us instead (Linux does
	 * the same when PPE thresholds are absent). ieee80211_add_hecaps()
	 * switches phy_cap_info[0] to the 2.4 GHz bits on a 2.4 GHz channel. */
	memset(&ic->ic_he_cap_elem, 0, sizeof(ic->ic_he_cap_elem));
	ic->ic_he_cap_elem.mac_cap_info[0] = IEEE80211_HE_MAC_CAP0_HTC_HE;
	ic->ic_he_cap_elem.mac_cap_info[1] = IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_16US;
	ic->ic_he_cap_elem.mac_cap_info[3] = IEEE80211_HE_MAC_CAP3_OMI_CONTROL |
	    IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_RESERVED;	/* = EXT_3 in Linux */
	ic->ic_he_cap_elem.mac_cap_info[4] = IEEE80211_HE_MAC_CAP4_AMDSU_IN_AMPDU;
	ic->ic_he_cap_elem.phy_cap_info[0] =
	    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G |
	    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G |	/* is_mt7922 in Linux */
	    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_RU_MAPPING_IN_5G;
	ic->ic_he_cap_elem.phy_cap_info[1] = IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD |
	    IEEE80211_HE_PHY_CAP1_DEVICE_CLASS_A |
	    IEEE80211_HE_PHY_CAP1_HE_LTF_AND_GI_FOR_HE_PPDUS_0_8US;
	ic->ic_he_cap_elem.phy_cap_info[2] = IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US |
	    IEEE80211_HE_PHY_CAP2_STBC_TX_UNDER_80MHZ |
	    IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ |
	    IEEE80211_HE_PHY_CAP2_UL_MU_FULL_MU_MIMO |
	    IEEE80211_HE_PHY_CAP2_UL_MU_PARTIAL_MU_MIMO;
	ic->ic_he_cap_elem.phy_cap_info[3] = IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_QPSK |
	    IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_QPSK;
	ic->ic_he_cap_elem.phy_cap_info[4] = IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE |
	    IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_4 |
	    IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_4;
	ic->ic_he_cap_elem.phy_cap_info[5] = IEEE80211_HE_PHY_CAP5_NG16_SU_FEEDBACK |
	    IEEE80211_HE_PHY_CAP5_NG16_MU_FEEDBACK;
	ic->ic_he_cap_elem.phy_cap_info[6] = IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_42_SU |
	    IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_75_MU |
	    IEEE80211_HE_PHY_CAP6_TRIG_CQI_FB |
	    IEEE80211_HE_PHY_CAP6_PARTIAL_BW_EXT_RANGE;
	ic->ic_he_cap_elem.phy_cap_info[7] = IEEE80211_HE_PHY_CAP7_POWER_BOOST_FACTOR_AR |
	    IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI;
	ic->ic_he_cap_elem.phy_cap_info[8] = IEEE80211_HE_PHY_CAP8_20MHZ_IN_40MHZ_HE_PPDU_IN_2G |
	    IEEE80211_HE_PHY_CAP8_20MHZ_IN_160MHZ_HE_PPDU | IEEE80211_HE_PHY_CAP8_80MHZ_IN_160MHZ_HE_PPDU |
	    IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_484;
	ic->ic_he_cap_elem.phy_cap_info[9] = IEEE80211_HE_PHY_CAP9_LONGER_THAN_16_SIGB_OFDM_SYM |
	    IEEE80211_HE_PHY_CAP9_NON_TRIGGERED_CQI_FEEDBACK |
	    IEEE80211_HE_PHY_CAP9_TX_1024_QAM_LESS_THAN_242_TONE_RU |
	    IEEE80211_HE_PHY_CAP9_RX_1024_QAM_LESS_THAN_242_TONE_RU |
	    IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_COMP_SIGB |
	    IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_NON_COMP_SIGB |
	    IEEE80211_HE_PHY_CAP9_NOMIMAL_PKT_PADDING_16US;
	memset(&ic->ic_he_mcs_nss_supp, 0xff, sizeof(ic->ic_he_mcs_nss_supp));
	memset(ic->ic_ppe_thres, 0, sizeof(ic->ic_ppe_thres));	/* MCS map: mwx_preinit */
	/* 160 MHz is MT7922 only (Linux: is_mt7922 in mt7921_init_he_caps and
	 * mt7921_register_device); MT7921/MT7921K stay at 80 MHz. */
	if (sc->sc_hwtype != MWX_HW_MT7922) {
		ic->ic_vhtcaps &= ~(IEEE80211_VHTCAP_SUPP_CHAN_WIDTH_MASK | IEEE80211_VHTCAP_SHORT_GI_160);
		ic->ic_he_cap_elem.phy_cap_info[0] &= ~IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G;
		ic->ic_he_cap_elem.phy_cap_info[4] &= ~IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_4;
		ic->ic_he_cap_elem.phy_cap_info[8] &= ~(IEEE80211_HE_PHY_CAP8_20MHZ_IN_160MHZ_HE_PPDU |
		    IEEE80211_HE_PHY_CAP8_80MHZ_IN_160MHZ_HE_PPDU);
	}

#if NOTYET
	ic->ic_vhtcaps = IEEE80211_VHTCAP_MAX_MPDU_LENGTH_11454 |
	    (IEEE80211_VHTCAP_MAX_AMPDU_LEN_64K <<
	    IEEE80211_VHTCAP_MAX_AMPDU_LEN_SHIFT) |
	    (IEEE80211_VHTCAP_CHAN_WIDTH_80 <<
	    IEEE80211_VHTCAP_CHAN_WIDTH_SHIFT) | IEEE80211_VHTCAP_SGI80 |
	    IEEE80211_VHTCAP_RX_ANT_PATTERN | IEEE80211_VHTCAP_TX_ANT_PATTERN;
#endif

	/* IBSS channel undefined for now. */
	ic->ic_ibss_chan = &ic->ic_channels[1];

	/* HW supports up to 288 STAs in HostAP and IBSS modes */
	ic->ic_max_aid = min(IEEE80211_AID_MAX, MWX_WTBL_STA - 1);

	ic->ic_max_rssi = 100;	/* rxi_rssi is normalized for ni_rssi. */

	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = mwx_ioctl;
	ifp->if_start = mwx_start;
	ifp->if_watchdog = mwx_watchdog;
	memcpy(ifp->if_xname, DEVNAME(sc), IFNAMSIZ);

	if_attach(ifp);
	ieee80211_ifattach(ifp, gMwxHal->mwxController());
	sc->sc_net_attached = true;
	ieee80211_media_init(ifp);
	MWX_DEV_LOG(sc, "attach net80211 attached caps=0x%x max_aid=%u\n",
	    ic->ic_caps, ic->ic_max_aid);

#if NBPFILTER > 0
	mwx_radiotap_attach(sc);
#endif

	ic->ic_node_alloc = mwx_node_alloc;
	ic->ic_newassoc = mwx_newassoc;
#ifndef IEEE80211_STA_ONLY
	ic->ic_node_leave = mwx_node_leave;
#endif
	ic->ic_bgscan_start = mwx_bgscan;
	ic->ic_bgscan_done = mwx_bgscan_done;
	ic->ic_set_key = mwx_set_key;
	ic->ic_ampdu_rx_start = mwx_ampdu_rx_start;	/* R9T trace32: RX BA */
	ic->ic_ampdu_rx_stop = mwx_ampdu_rx_stop;
	ic->ic_ampdu_tx_start = mwx_ampdu_tx_start;	/* R9T trace37 */
	ic->ic_ampdu_tx_stop = mwx_ampdu_tx_stop;
	ic->ic_delete_key = mwx_delete_key;

	/* Override 802.11 state transition machine. */
	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = mwx_newstate;

	sc->sc_nswq = taskq_create("mwxns", 1, IPL_NET, 0);
	if (sc->sc_nswq == NULL) {
		smtw_trace("%s: can't create task queue\n", DEVNAME(sc));
		goto fail;
	}

	task_set(&sc->sc_newstate_task, mwx_newstate_task, sc, "mwx_newstate");
	task_set(&sc->sc_bgscan_done_task, mwx_bgscan_done_task, sc, "mwx_bgscan");
	task_set(&sc->sc_setkey_task, mwx_setkey_task, sc, "mwx_setkey");
	task_set(&sc->sc_ba_task, mwx_ba_task, sc, "mwx_ba");
	task_set(&sc->sc_reset_task, mwx_reset_task, sc, "mwx_reset");
	task_set(&sc->sc_scan_task, mwx_end_scan_task, sc, "mwx_scan");
	timeout_set(&sc->sc_reset_to, mwx_reset_timeo, sc);

	/*
	 * We cannot read the MAC address without loading the
	 * firmware from disk. Postpone until mountroot is done.
	 */
	config_mountroot(self, mwx_attach_hook);

	MWX_DEV_LOG(sc, "attach complete\n");
	return true;

fail:
	/* The HAL caller owns partial-attach cleanup through detach(). */
	return false;
}

int
mwx_activate(struct device *self, int act)
{
	/* XXX TODO */
	return 0;
}

#if 0
/* OpenBSD autoconf metadata is replaced by YYavicMtkWlan's IOKit matching path. */
struct cfdriver mwx_cd = {
	NULL, "mwx", DV_IFNET
};

struct cfattach mwx_ca = {
	sizeof(struct mwx_softc), mwx_match, mwx_attach,
	NULL, mwx_activate
};
#endif

void
mwx_reset(struct mwx_softc *sc)
{
	if (sc->sc_resetting || sc->sc_detaching)
		return;
	sc->sc_resetting = 1;
	task_add(sc->sc_nswq, &sc->sc_reset_task);
}

void
mwx_reset_timeo(void *arg)
{
	mwx_reset((struct mwx_softc *)arg);
}

void
mwx_reset_task(void *arg)
{
	struct mwx_softc *sc = (struct mwx_softc *)arg;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int fatal = 0;

	if (ifp->if_flags & IFF_RUNNING)
		mwx_stop(ifp);

	if (!fatal && (ifp->if_flags & (IFF_UP | IFF_RUNNING)) == IFF_UP)
		mwx_init(ifp);
	sc->sc_resetting = 0;
	sc->sc_coredump_cnt = 0;
}

int
mwx_txwi_alloc(struct mwx_softc *sc, int count)
{
	int error, nsegs, i;
	struct mwx_txwi_desc *q = &sc->sc_txwi;
	bus_size_t size = count * sizeof(*q->mt_desc);
	uint32_t addr;

	MWX_DEV_LOG(sc, "txwi alloc start count=%d size=%zu\n",
	    count, (size_t)size);
	LIST_INIT(&q->mt_freelist);
	q->mt_count = count;

	error = bus_dmamap_create(sc->sc_dmat, size, 1, size, 0,
	    BUS_DMA_NOWAIT, &q->mt_map);
	if (error != 0) {
		smtw_trace("%s: could not create desc TWXI map\n", DEVNAME(sc));
		goto fail;
	}

	error = bus_dmamem_alloc(sc->sc_dmat, size, PAGE_SIZE, 0, &q->mt_seg,
	    1, &nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO);
	if (error != 0) {
		smtw_trace("%s: could not allocate TWXI memory\n", DEVNAME(sc));
		goto fail;
	}

	error = bus_dmamem_map(sc->sc_dmat, &q->mt_seg, nsegs, size,
	    (void **)&q->mt_desc, BUS_DMA_NOWAIT);
	if (error != 0) {
		smtw_trace("%s: can't map desc DMA memory\n", DEVNAME(sc));
		goto fail;
	}

	error = bus_dmamap_load(sc->sc_dmat, q->mt_map, q->mt_desc,
	    size, NULL, BUS_DMA_NOWAIT);
	if (error != 0) {
		smtw_trace("%s: could not load desc DMA map\n", DEVNAME(sc));
		goto fail;
	}

	addr = q->mt_map->dm_segs[0].ds_addr;

	q->mt_data = (struct mwx_txwi *)mallocarray(count, sizeof(*q->mt_data),
	    M_DEVBUF, M_NOWAIT | M_ZERO);
	if (q->mt_data == NULL) {
		smtw_trace("%s: could not allocate soft data\n", DEVNAME(sc));
		error = ENOMEM;
		goto fail;
	}

	for (i = 0; i < count; i++) {
		q->mt_data[i].mt_idx = i;
		q->mt_data[i].mt_desc = &q->mt_desc[i];
		q->mt_data[i].mt_addr = addr + i * sizeof(*q->mt_desc);
		error = bus_dmamap_create(sc->sc_dmat, MT_TXD_LEN_MASK,
		    MT_MAX_SCATTER, MT_TXD_LEN_MASK, 0, BUS_DMA_NOWAIT,
		    &q->mt_data[i].mt_map);
		if (error != 0) {
			smtw_trace("%s: could not create data DMA map\n",
			    DEVNAME(sc));
			goto fail;
		}
	}

	for (i = count - 1; i >= MT_PACKET_ID_FIRST; i--)
		LIST_INSERT_HEAD(&q->mt_freelist, &q->mt_data[i], mt_entry);

	MWX_DEV_LOG(sc, "txwi alloc ok count=%d dma=0x%08x\n", count, addr);
	return 0;

fail:
	MWX_DEV_LOG(sc, "txwi alloc failed error=%d\n", error);
	mwx_txwi_free(sc);
	return error;
}

void
mwx_txwi_free(struct mwx_softc *sc)
{
	struct mwx_txwi_desc *q = &sc->sc_txwi;

	if (q->mt_data != NULL) {
		int i;
		for (i = 0; i < q->mt_count; i++) {
			struct mwx_txwi *mt = &q->mt_data[i];
			bus_dmamap_destroy(sc->sc_dmat, mt->mt_map);
			m_freem(mt->mt_mbuf);
			if (i >= MT_PACKET_ID_FIRST)
				LIST_REMOVE(mt, mt_entry);
		}
		free(q->mt_data, M_DEVBUF, q->mt_count * sizeof(*q->mt_data));
	}

	if (q->mt_desc != NULL) {
		bus_dmamap_sync(sc->sc_dmat, q->mt_map, 0,
			q->mt_map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, q->mt_map);
	}

	/*
	 * XXX TODO this is probably not correct as a check, should use
	 * some state variable bitfield to decide which steps need to be run.
	 */
	if (q->mt_seg.ds_len != 0)
		bus_dmamem_unmap(sc->sc_dmat, (caddr_t)q->mt_desc,
			q->mt_count * sizeof(*q->mt_desc));
	if (q->mt_map != NULL)
		bus_dmamem_free(sc->sc_dmat, &q->mt_seg, 1);

	memset(q, 0, sizeof(*q));
}

struct mwx_txwi *
mwx_txwi_get(struct mwx_softc *sc)
{
	struct mwx_txwi *mt;

	mt = LIST_FIRST(&sc->sc_txwi.mt_freelist);
	if (mt == NULL)
		return NULL;
	LIST_REMOVE(mt, mt_entry);
	mt->mt_busy = 1;
	if (++gTxwiUse > gTxwiPeak)
		gTxwiPeak = gTxwiUse;
	return mt;
}

void
mwx_txwi_put(struct mwx_softc *sc, struct mwx_txwi *mt)
{
	if (mt->mt_busy == 0)
		return;
	gTxwiUse--;

	if (mt->mt_mbuf != NULL) {
		bus_dmamap_sync(sc->sc_dmat, mt->mt_map, 0,
		    mt->mt_map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, mt->mt_map);
		m_freem(mt->mt_mbuf);
		mt->mt_mbuf = NULL;
	}

	memset(mt->mt_desc, 0, sizeof(*mt->mt_desc));
	mt->mt_busy = 0;

	if (mt->mt_idx < MT_PACKET_ID_FIRST)
		return;
	LIST_INSERT_HEAD(&sc->sc_txwi.mt_freelist, mt, mt_entry);
}

int
mwx_txwi_enqueue(struct mwx_softc *sc, struct mwx_txwi *mt, struct mbuf **mp)
{
	struct mwx_txwi_desc *q = &sc->sc_txwi;
	struct mt76_txwi *txp = mt->mt_desc;
	struct mt76_connac_txp_ptr *ptr = &txp->ptr[0];
	struct mbuf *m = *mp;
	uint32_t addr;
	uint16_t len;
	int i, nsegs, rv;

	rv = bus_dmamap_load_mbuf(sc->sc_dmat, mt->mt_map, m,
	    BUS_DMA_WRITE | BUS_DMA_NOWAIT);
	if (rv != 0)
		return EFBIG;

	nsegs = mt->mt_map->dm_nsegs;
	/*
	 * R9T: macOS bus_dma emulasyonu MT_MAX_SCATTER (4) sinirini uygulamiyor; 5+ parcali
	 * cercevede TXP disina (sonraki txwi yuvasina) yaziliyor ve LAST bayragi cipe hic
	 * ulasmiyordu (log: len=72/8/8/20). Cerceveyi tek bitisik tampona kopyala.
	 */
	if (nsegs > MT_HW_TXP_MAX_BUF_NUM) {
		static uint32_t lin_log;
		size_t plen = mbuf_pkthdr_len(m);
		unsigned int chunks = 1;
		mbuf_t n = NULL;

		bus_dmamap_unload(sc->sc_dmat, mt->mt_map);
		if (mbuf_allocpacket(MBUF_DONTWAIT, plen, &chunks, &n) != 0 || n == NULL)
			return ENOBUFS;
		if (mbuf_copydata(m, 0, plen, mbuf_data(n)) != 0) {
			mbuf_freem(n);
			return ENOBUFS;
		}
		mbuf_setlen(n, plen);
		mbuf_pkthdr_setlen(n, plen);
		if (lin_log < 20) {
			lin_log++;
			MWX_DEV_LOG(sc, "TX linearize nsegs=%d len=%zu\n", nsegs, plen);
		}
		m_freem(m);
		m = n;
		*mp = m;
		rv = bus_dmamap_load_mbuf(sc->sc_dmat, mt->mt_map, m,
		    BUS_DMA_WRITE | BUS_DMA_NOWAIT);
		if (rv != 0)
			return EFBIG;
		nsegs = mt->mt_map->dm_nsegs;
		if (nsegs > MT_HW_TXP_MAX_BUF_NUM) {
			bus_dmamap_unload(sc->sc_dmat, mt->mt_map);
			MWX_DEV_LOG(sc, "TX linearize failed nsegs=%d\n", nsegs);
			return EFBIG;
		}
	}

	bus_dmamap_sync(sc->sc_dmat, mt->mt_map, 0, mt->mt_map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	txp->msdu_id[0] = htole16(mt->mt_idx | MT_MSDU_ID_VALID);
	mt->mt_mbuf = m;

	bus_dmamap_sync(sc->sc_dmat, q->mt_map, 0, q->mt_map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);
	for (i = 0; i < nsegs; i++) {
		KASSERT(mt->mt_map->dm_segs[i].ds_addr <= UINT32_MAX);
		KASSERT(mt->mt_map->dm_segs[i].ds_len <= MT_TXD_LEN_MASK);
		addr = mt->mt_map->dm_segs[i].ds_addr;
		len = mt->mt_map->dm_segs[i].ds_len;

		if (i == nsegs - 1)
			len |= MT_TXD_LEN_LAST;

		if ((i & 1) == 0) {
			ptr->buf0 = htole32(addr);
			ptr->len0 = htole16(len);
		} else {
			ptr->buf1 = htole32(addr);
			ptr->len1 = htole16(len);
			ptr++;
		}
	}
	bus_dmamap_sync(sc->sc_dmat, q->mt_map, 0, q->mt_map->dm_mapsize,
	    BUS_DMASYNC_POSTWRITE);

	return 0;
}

int
mwx_queue_alloc(struct mwx_softc *sc, struct mwx_queue *q, int count,
    uint32_t regbase)
{
	int error, nsegs, i;
	bus_size_t size = count * sizeof(*q->mq_desc);

	MWX_DEV_LOG(sc, "queue alloc start regbase=0x%08x count=%d size=%zu\n",
	    regbase, count, (size_t)size);
	q->mq_regbase = regbase;
	q->mq_count = count;

	error = bus_dmamap_create(sc->sc_dmat, size, 1, size, 0,
	    BUS_DMA_NOWAIT, &q->mq_map);
	if (error != 0) {
		smtw_trace("%s: could not create desc DMA map\n", DEVNAME(sc));
		goto fail;
	}

	error = bus_dmamem_alloc(sc->sc_dmat, size, PAGE_SIZE, 0, &q->mq_seg,
	    1, &nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO);
	if (error != 0) {
		smtw_trace("%s: could not allocate DMA memory\n", DEVNAME(sc));
		goto fail;
	}

	error = bus_dmamem_map(sc->sc_dmat, &q->mq_seg, nsegs, size,
	    (void **)&q->mq_desc, BUS_DMA_NOWAIT);
	if (error != 0) {
		smtw_trace("%s: can't map desc DMA memory\n", DEVNAME(sc));
		goto fail;
	}

	error = bus_dmamap_load(sc->sc_dmat, q->mq_map, q->mq_desc,
	    size, NULL, BUS_DMA_NOWAIT);
	if (error != 0) {
		smtw_trace("%s: could not load desc DMA map\n", DEVNAME(sc));
		goto fail;
	}

	q->mq_data = (struct mwx_queue_data *)mallocarray(count, sizeof(*q->mq_data),
	    M_DEVBUF, M_NOWAIT | M_ZERO);
	if (q->mq_data == NULL) {
		smtw_trace("%s: could not allocate soft data\n", DEVNAME(sc));
		error = ENOMEM;
		goto fail;
	}

	for (i = 0; i < count; i++) {
		error = bus_dmamap_create(sc->sc_dmat, MT_MAX_SIZE,
		    MT_MAX_SCATTER, MT_MAX_SIZE, 0, BUS_DMA_NOWAIT,
		    &q->mq_data[i].md_map);
		if (error != 0) {
			smtw_trace("%s: could not create data DMA map\n",
			    DEVNAME(sc));
			goto fail;
		}
	}

	mwx_queue_reset(sc, q);
	MWX_DEV_LOG(sc, "queue alloc ok regbase=0x%08x count=%d dma=0x%llx\n",
	    regbase, count, (unsigned long long)q->mq_map->dm_segs[0].ds_addr);
	return 0;

fail:
	MWX_DEV_LOG(sc, "queue alloc failed regbase=0x%08x error=%d\n",
	    regbase, error);
	mwx_queue_free(sc, q);
	return error;
}

void
mwx_queue_free(struct mwx_softc *sc, struct mwx_queue *q)
{
	if (q->mq_data != NULL) {
		int i;
		for (i = 0; i < q->mq_count; i++) {
			struct mwx_queue_data  *md = &q->mq_data[i];
			bus_dmamap_destroy(sc->sc_dmat, md->md_map);
			m_freem(md->md_mbuf);
		}
		free(q->mq_data, M_DEVBUF, q->mq_count * sizeof(*q->mq_data));
	}

	if (q->mq_desc != NULL) {
		bus_dmamap_sync(sc->sc_dmat, q->mq_map, 0,
			q->mq_map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, q->mq_map);
	}

	/*
	 * XXX TODO this is probably not correct as a check, should use
	 * some state variable bitfield to decide which steps need to be run.
	 */
	if (q->mq_seg.ds_len != 0)
		bus_dmamem_unmap(sc->sc_dmat, (caddr_t)q->mq_desc,
			q->mq_count * sizeof(*q->mq_desc));
	if (q->mq_map != NULL)
		bus_dmamem_free(sc->sc_dmat, &q->mq_seg, 1);

	memset(q, 0, sizeof(*q));
}

void
mwx_queue_reset(struct mwx_softc *sc, struct mwx_queue *q)
{
	int i;
	uint32_t dmaaddr;
	struct mwx_queue_data *md;

	/* clear descriptors */
	bus_dmamap_sync(sc->sc_dmat, q->mq_map, 0, q->mq_map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	for (i = 0; i < q->mq_count; i++) {
		q->mq_desc[i].buf0 = 0;
		q->mq_desc[i].buf1 = 0;
		q->mq_desc[i].info = 0;
		q->mq_desc[i].ctrl = htole32(MT_DMA_CTL_DMA_DONE);
	}

	bus_dmamap_sync(sc->sc_dmat, q->mq_map, 0, q->mq_map->dm_mapsize,
	    BUS_DMASYNC_POSTWRITE);

	/* reset DMA registers */
	KASSERT(q->mq_map->dm_nsegs == 1);
	KASSERT(q->mq_map->dm_segs[0].ds_addr <= UINT32_MAX);
	dmaaddr	= q->mq_map->dm_segs[0].ds_addr;
	mwx_write(sc, q->mq_regbase + MT_DMA_DESC_BASE, dmaaddr);
	mwx_write(sc, q->mq_regbase + MT_DMA_RING_SIZE, q->mq_count);
	mwx_write(sc, q->mq_regbase + MT_DMA_CPU_IDX, 0);
	mwx_write(sc, q->mq_regbase + MT_DMA_DMA_IDX, 0);
	q->mq_cons = 0;
	q->mq_prod = 0;

	/* free buffers */
	{
		IOLock *ql = mwx_qlock();
		if (ql != NULL)
			IOLockLock(ql);
		for (i = 0; i < q->mq_count; i++) {
			md = &q->mq_data[i];
			if (md->md_mbuf != NULL) {
				bus_dmamap_sync(sc->sc_dmat, md->md_map, 0,
				    md->md_map->dm_mapsize,
				    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
				bus_dmamap_unload(sc->sc_dmat, md->md_map);
				m_freem(md->md_mbuf);
				md->md_mbuf = NULL;
			}
		}
		if (ql != NULL)
			IOLockUnlock(ql);
	}
}

int
mwx_buf_fill(struct mwx_softc *sc, struct mwx_queue_data *md,
    struct mwx_desc *desc)
{
	struct mbuf *m;
	uint32_t buf0, len0, ctrl;
	int rv;

	m = MCLGETL(NULL, M_DONTWAIT, MT_RX_BUF_SIZE);
	if (m == NULL)
		return (ENOMEM);

	mbuf_setlen(m, MT_RX_BUF_SIZE);
	mbuf_pkthdr_setlen(m, MT_RX_BUF_SIZE);

	rv = bus_dmamap_load_mbuf(sc->sc_dmat, md->md_map, m,
	     BUS_DMA_READ | BUS_DMA_NOWAIT);
	if (rv != 0) {
		smtw_trace("%s: could not load data, %d\n", DEVNAME(sc), rv);
		m_freem(m);
		return (rv);
	}

	bus_dmamap_sync(sc->sc_dmat, md->md_map, 0,
	    md->md_map->dm_mapsize, BUS_DMASYNC_PREREAD);

	md->md_mbuf = m;

	KASSERT(md->md_map->dm_nsegs == 1);
	KASSERT(md->md_map->dm_segs[0].ds_addr <= UINT32_MAX);
	buf0 = md->md_map->dm_segs[0].ds_addr;
	len0 = md->md_map->dm_segs[0].ds_len;
	ctrl = MT_DMA_CTL_SD_LEN0(len0);
	ctrl |= MT_DMA_CTL_LAST_SEC0;

	desc->buf0 = htole32(buf0);
	desc->buf1 = 0;
	desc->info = 0;
	desc->ctrl = htole32(ctrl);

	return 0;
}

int
mwx_queue_fill(struct mwx_softc *sc, struct mwx_queue *q)
{
	u_int idx, last;
	int rv;

	last = (q->mq_count + q->mq_cons - 1) % q->mq_count;
	idx = q->mq_prod;

	bus_dmamap_sync(sc->sc_dmat, q->mq_map, 0, q->mq_map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	rv = 0;
	while (idx != last) {
		rv = mwx_buf_fill(sc, &q->mq_data[idx], &q->mq_desc[idx]);
		if (rv != 0) {
			smtw_trace("%s: could not fill data, slot %d err %d\n",
			    DEVNAME(sc), idx, rv);
			gYYRxFillFail++;
			break;
		}
		idx = (idx + 1) % q->mq_count;
	}

	bus_dmamap_sync(sc->sc_dmat, q->mq_map, 0, q->mq_map->dm_mapsize,
	    BUS_DMASYNC_POSTWRITE);

	/*
	 * YYavic: publish what was filled even when an allocation failed. The
	 * old code returned before moving the CPU index, so the descriptors of
	 * this pass never reached the hardware; once it consumed the rest, RX
	 * (and the TX-free events that arrive on it) stopped for good. The poll
	 * timer retries the missing slots (mwx_rx_refill_check).
	 */
	if (idx != q->mq_prod) {
		q->mq_prod = idx;
		mwx_write(sc, q->mq_regbase + MT_DMA_CPU_IDX, q->mq_prod);
	}

	return rv;
}

int
mwx_dma_alloc(struct mwx_softc *sc)
{
	int rv;

	MWX_DEV_LOG(sc, "dma alloc start\n");
	/* TX queues */
	if ((rv = mwx_queue_alloc(sc, &sc->sc_txq, MWX_TX_RING_COUNT,
	    MT_TX_DATA_RING_BASE)) != 0)
		return rv;
	if ((rv = mwx_queue_alloc(sc, &sc->sc_txmcuq, 256,
	    MT_TX_MCU_RING_BASE)) != 0)
		return rv;
	if ((rv = mwx_queue_alloc(sc, &sc->sc_txfwdlq, 16 /* XXX */,
	    MT_TX_FWDL_RING_BASE)) != 0)
		return rv;

	/* RX queues */
	if ((rv = mwx_queue_alloc(sc, &sc->sc_rxq, MWX_RX_RING_COUNT,
	    MT_RX_DATA_RING_BASE)) != 0 ||
	    (rv = mwx_queue_fill(sc, &sc->sc_rxq)) != 0)
		return rv;
	if ((rv = mwx_queue_alloc(sc, &sc->sc_rxmcuq, 256,
	    MT_RX_MCU_RING_BASE)) != 0 ||
	    (rv = mwx_queue_fill(sc, &sc->sc_rxmcuq)) != 0)
		return rv;
	if ((rv = mwx_queue_alloc(sc, &sc->sc_rxfwdlq, 16 /* XXX */,
	    MT_RX_FWDL_RING_BASE)) != 0 ||
	    (rv = mwx_queue_fill(sc, &sc->sc_rxfwdlq)) != 0)
		return rv;

	/* enable DMA engine */
	mwx_dma_enable(sc);

	MWX_DEV_LOG(sc, "dma alloc complete\n");
	return 0;
}

int
mwx_dma_reset(struct mwx_softc *sc, int fullreset)
{
	int rv;

	DPRINTF("%s: DMA reset\n", DEVNAME(sc));

	if ((rv = mwx_dma_disable(sc, fullreset)) != 0)
		return rv;
	if (fullreset)
		if ((rv = mwx_wfsys_reset(sc)) != 0)
			return rv;

	/* TX queues */
	mwx_queue_reset(sc, &sc->sc_txq);
	mwx_queue_reset(sc, &sc->sc_txmcuq);
	mwx_queue_reset(sc, &sc->sc_txfwdlq);

	/* RX queues */
	mwx_queue_reset(sc, &sc->sc_rxq);
	mwx_queue_reset(sc, &sc->sc_rxmcuq);
	mwx_queue_reset(sc, &sc->sc_rxfwdlq);

	/* TDOD mt76_tx_status_check */

	/* refill RX queues */
	if ((rv = mwx_queue_fill(sc, &sc->sc_rxq)) != 0 ||
	    (rv = mwx_queue_fill(sc, &sc->sc_rxmcuq)) != 0 ||
	    (rv = mwx_queue_fill(sc, &sc->sc_rxfwdlq)) != 0)
		return rv;

	/* enable DMA engine */
	mwx_dma_enable(sc);

	return 0;
}

void
mwx_dma_free(struct mwx_softc *sc)
{
	/* TX queues */
	mwx_queue_free(sc, &sc->sc_txq);
	mwx_queue_free(sc, &sc->sc_txmcuq);
	mwx_queue_free(sc, &sc->sc_txfwdlq);

	/* RX queues */
	mwx_queue_free(sc, &sc->sc_rxq);
	mwx_queue_free(sc, &sc->sc_rxmcuq);
	mwx_queue_free(sc, &sc->sc_rxfwdlq);
}

static inline int
mwx_dma_free_slots(struct mwx_queue *q)
{
	int free = q->mq_count - 1;
	free += q->mq_cons;
	free -= q->mq_prod;
	free %= q->mq_count;
	return free;
}

int
mwx_dma_tx_enqueue(struct mwx_softc *sc, struct mwx_queue *q, struct mbuf *m)
{
	struct mwx_queue_data *md;
	struct mwx_desc *desc;
	int i, nsegs, idx, rv;

	idx = q->mq_prod;
	md = &q->mq_data[idx];
	desc = &q->mq_desc[idx];

	rv = bus_dmamap_load_mbuf(sc->sc_dmat, md->md_map, m,
	    BUS_DMA_WRITE | BUS_DMA_NOWAIT);
	if (rv == EFBIG && m_defrag(m, M_DONTWAIT) == 0)
		rv = bus_dmamap_load_mbuf(sc->sc_dmat, md->md_map, m,
		    BUS_DMA_WRITE | BUS_DMA_NOWAIT);
	if (rv != 0) {
		static int enq_fail_log;
		if (enq_fail_log < 16) {
			enq_fail_log++;
			MWX_LOG("tx_enqueue map load FAIL rv=%d pktlen=%d q=0x%x\n", rv,
			    (int)mbuf_pkthdr_len(m), q->mq_regbase);
		}
		return rv;
	}

	nsegs = md->md_map->dm_nsegs;

	/* check if there is enough space */
	if ((nsegs + 1)/2 > mwx_dma_free_slots(q)) {
		bus_dmamap_unload(sc->sc_dmat, md->md_map);
		return EBUSY;
	}

	bus_dmamap_sync(sc->sc_dmat, md->md_map, 0, md->md_map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);
	md->md_mbuf = m;
	md->md_txwi = NULL;

	bus_dmamap_sync(sc->sc_dmat, q->mq_map, 0, q->mq_map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);
	for (i = 0; i < nsegs; i += 2) {
		uint32_t	buf0, buf1 = 0;
		uint32_t	len0, len1 = 0, ctrl;

		KASSERT(md->md_map->dm_segs[i].ds_addr <= UINT32_MAX);
		buf0 = md->md_map->dm_segs[i].ds_addr;
		len0 = md->md_map->dm_segs[i].ds_len;
		ctrl = MT_DMA_CTL_SD_LEN0(len0);

		if (i < nsegs - 1) {
			KASSERT(md->md_map->dm_segs[i + 1].ds_addr <=
			    UINT32_MAX);
			buf1 = md->md_map->dm_segs[i + 1].ds_addr;
			len1 = md->md_map->dm_segs[i + 1].ds_len;
			ctrl |= MT_DMA_CTL_SD_LEN1(len1);
		}

		if (i == nsegs - 1)
			ctrl |= MT_DMA_CTL_LAST_SEC0;
		else if (i == nsegs - 2)
			ctrl |= MT_DMA_CTL_LAST_SEC1;

		desc->buf0 = htole32(buf0);
		desc->buf1 = htole32(buf1);
		desc->info = 0;
		desc->ctrl = htole32(ctrl);

		idx = (idx + 1) % q->mq_count;
		KASSERT(idx != q->mq_cons);
		md = &q->mq_data[idx];
		desc = &q->mq_desc[idx];
	}
	bus_dmamap_sync(sc->sc_dmat, q->mq_map, 0, q->mq_map->dm_mapsize,
	    BUS_DMASYNC_POSTWRITE);

	q->mq_prod = idx;

	mwx_write(sc, q->mq_regbase + MT_DMA_CPU_IDX, q->mq_prod);

	return 0;
}

int
mwx_dma_txwi_enqueue(struct mwx_softc *sc, struct mwx_queue *q,
    struct mwx_txwi *mt)
{
	struct mwx_queue_data *md;
	struct mwx_desc *desc;
	uint32_t buf0, len0, ctrl;
	int idx;

	idx = q->mq_prod;
	md = &q->mq_data[idx];
	desc = &q->mq_desc[idx];

	/* check if there is enough space */
	if (1 > mwx_dma_free_slots(q)) {
		bus_dmamap_unload(sc->sc_dmat, md->md_map);
		return EBUSY;
	}

	md->md_txwi = mt;
	md->md_mbuf = NULL;

	bus_dmamap_sync(sc->sc_dmat, q->mq_map, 0, q->mq_map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	buf0 = mt->mt_addr;
	len0 = sizeof(*mt->mt_desc);
	ctrl = MT_DMA_CTL_SD_LEN0(len0);
	ctrl |= MT_DMA_CTL_LAST_SEC0;

	static uint32_t txwi_dma_log_count;
	if (txwi_dma_log_count < 16) {
		MWX_DEV_LOG(sc, "txwi dma idx=%d addr=0x%08x len=%u\n",
		    idx, buf0, len0);
		txwi_dma_log_count++;
	}

	desc->buf0 = htole32(buf0);
	desc->buf1 = 0;
	desc->info = 0;
	desc->ctrl = htole32(ctrl);

	idx = (idx + 1) % q->mq_count;
	KASSERT(idx != q->mq_cons);

	bus_dmamap_sync(sc->sc_dmat, q->mq_map, 0, q->mq_map->dm_mapsize,
	    BUS_DMASYNC_POSTWRITE);

	q->mq_prod = idx;

	mwx_write(sc, q->mq_regbase + MT_DMA_CPU_IDX, q->mq_prod);

	return 0;
}

void
mwx_dma_tx_cleanup(struct mwx_softc *sc, struct mwx_queue *q)
{
	struct mwx_queue_data *md;
	struct mwx_desc *desc;
	int idx, last;
	IOLock *ql = mwx_qlock();

	if (ql != NULL)
		IOLockLock(ql);
	idx = q->mq_cons;
	last = mwx_read(sc, q->mq_regbase + MT_DMA_DMA_IDX);

	if (idx == last) {
		if (ql != NULL)
			IOLockUnlock(ql);
		return;
	}

	bus_dmamap_sync(sc->sc_dmat, q->mq_map, 0, q->mq_map->dm_mapsize,
	     BUS_DMASYNC_PREWRITE);

	while (idx != last) {
		md = &q->mq_data[idx];
		desc = &q->mq_desc[idx];

		if (md->md_mbuf != NULL) {
			bus_dmamap_sync(sc->sc_dmat, md->md_map, 0,
			    md->md_map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, md->md_map);
			m_freem(md->md_mbuf);
			md->md_mbuf = NULL;
		}
		if (md->md_txwi != NULL) {
			static uint32_t txwi_ack_log_count;

			/* TXWI cleanup is driven by the firmware TX free event. */
			md->md_txwi = NULL;
			if (txwi_ack_log_count < 200) {
				MWX_DEV_LOG(sc, "txwi acked idx=%d\n", idx);
				txwi_ack_log_count++;
			}
		}

		/* clear DMA desc just to be sure */
		desc->buf0 = 0;
		desc->buf1 = 0;
		desc->info = 0;
		desc->ctrl = htole32(MT_DMA_CTL_DMA_DONE);

		idx = (idx + 1) % q->mq_count;

		/* check if more data made it in */
		/* XXX should we actually do that? */
		if (idx == last)
			last = mwx_read(sc, q->mq_regbase + MT_DMA_DMA_IDX);
	}

	bus_dmamap_sync(sc->sc_dmat, q->mq_map, 0, q->mq_map->dm_mapsize,
	    BUS_DMASYNC_POSTWRITE);

	q->mq_cons = idx;
	if (q->mq_wakeme) {
		q->mq_wakeme = 0;
		wakeup(q);
	}
	if (ql != NULL)
		IOLockUnlock(ql);
}

void
mwx_dma_tx_done(struct mwx_softc *sc)
{
	uint64_t yy_t0 = mach_absolute_time();
	mwx_dma_tx_cleanup(sc, &sc->sc_txq);
	mwx_dma_tx_cleanup(sc, &sc->sc_txmcuq);
	mwx_dma_tx_cleanup(sc, &sc->sc_txfwdlq);

	/* XXX do we need to wakeup someone */
	gYYPfTxDone += mach_absolute_time() - yy_t0;
}

/* XXX wrong place */
void
mwx_dma_rx_process(struct mwx_softc *sc, struct mbuf_list *ml)
{
	struct mbuf_list mlout = MBUF_LIST_INITIALIZER();
	struct mbuf *m;
	uint32_t *data, rxd, type, flag;
	uint32_t event_count = 0, notify_count = 0, normal_mcu_count = 0;
	uint32_t normal_count = 0, unknown_count = 0;
	uint32_t first_unknown_type = 0, first_unknown_flag = 0, first_unknown_rxd = 0;

	while ((m = ml_dequeue(ml)) != NULL) {
		data = mtod(m, uint32_t *);
		rxd = le32toh(data[0]);

		type = MT_RXD0_PKT_TYPE_GET(rxd);
		flag = (rxd & MT_RXD0_PKT_FLAG_MASK) >> MT_RXD0_PKT_FLAG_SHIFT;

		if (type == PKT_TYPE_RX_EVENT && flag == 0x1)
			type = PKT_TYPE_NORMAL_MCU;

		switch (type) {
		case PKT_TYPE_RX_EVENT:
			event_count++;
			mwx_mcu_rx_event(sc, m);
			break;
		case PKT_TYPE_TXRX_NOTIFY:
			notify_count++;
			mwx_mac_tx_free(sc, m);
			break;
		case PKT_TYPE_TXS:
#if TODO
			for (rxd += 2; rxd + 8 <= end; rxd += 8)
				mt7921_mac_add_txs(dev, rxd);
#endif
			m_freem(m);
			break;
		case PKT_TYPE_NORMAL_MCU:
			normal_mcu_count++;
			mwx_rx(sc, m, &mlout);
			break;
		case PKT_TYPE_NORMAL:
			normal_count++;
			mwx_rx(sc, m, &mlout);
			break;
		default:
			if (unknown_count == 0) {
				first_unknown_type = type;
				first_unknown_flag = flag;
				first_unknown_rxd = rxd;
			}
			unknown_count++;
			m_freem(m);
			break;
		}
	}

	if (event_count || notify_count || normal_mcu_count ||
	    normal_count || unknown_count) {
		sc->sc_rx_pkt_event += event_count;
		sc->sc_rx_pkt_txrx_notify += notify_count;
		sc->sc_rx_pkt_normal_mcu += normal_mcu_count;
		sc->sc_rx_pkt_normal += normal_count;
		sc->sc_rx_pkt_unknown += unknown_count;
		static uint32_t rxp_log;	/* R9T trace32: log seli kanitlari siliyordu; sinirla */
		if ((sc->sc_rx_process_events < 64 || unknown_count != 0 ||
		    (normal_count != 0 && rxp_log < 40)) && rxp_log++ < 200) {
			MWX_DEV_LOG(sc,
			    "rx process event=%u notify=%u normal_mcu=%u normal=%u unknown=%u totals e=%u n=%u nmcu=%u norm=%u unk=%u\n",
			    event_count, notify_count, normal_mcu_count,
			    normal_count, unknown_count, sc->sc_rx_pkt_event,
			    sc->sc_rx_pkt_txrx_notify,
			    sc->sc_rx_pkt_normal_mcu, sc->sc_rx_pkt_normal,
			    sc->sc_rx_pkt_unknown);
			if (unknown_count != 0) {
				MWX_DEV_LOG(sc,
				    "rx unknown first type=%u flag=0x%x rxd0=0x%08x\n",
				    first_unknown_type, first_unknown_flag,
				    first_unknown_rxd);
			}
		}
		sc->sc_rx_process_events++;
	}

	/* YYavic: an A-MSDU open at the end of the batch stays open -- its
	 * remaining subframes come with the next batch (flushing it here made
	 * them a second MPDU with the same SN, dropped below the BA window).
	 * The poll timer flushes one that never completes. */
	if (sc->sc_amsdu_n > 0)
		sc->sc_amsdu_idle++;
	uint64_t ti = mach_absolute_time();
	if_input(&sc->sc_ic.ic_if, &mlout);
	gYYPfIfIn += mach_absolute_time() - ti;
}

void
mwx_dma_rx_dequeue(struct mwx_softc *sc, struct mwx_queue *q,
    struct mbuf_list *ml)
{
	struct mwx_queue_data *md;
	struct mwx_desc *desc;
	struct mbuf *m, *m0 = NULL, *mtail = NULL;
	int idx, last;

	idx = q->mq_cons;
	last = mwx_read(sc, q->mq_regbase + MT_DMA_DMA_IDX);

	if (idx == last)
		return;

	bus_dmamap_sync(sc->sc_dmat, q->mq_map, 0, q->mq_map->dm_mapsize,
	     BUS_DMASYNC_PREREAD);

	while (idx != last) {
		uint32_t ctrl;

		md = &q->mq_data[idx];
		desc = &q->mq_desc[idx];

		bus_dmamap_sync(sc->sc_dmat, md->md_map, 0,
		    md->md_map->dm_mapsize, BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_dmat, md->md_map);

		/* dequeue mbuf */
		m = md->md_mbuf;
		md->md_mbuf = NULL;

		/* only buf0 is used on RX rings */
		ctrl = le32toh(desc->ctrl);
		mbuf_setlen(m, MT_DNA_CTL_SD_GET_LEN0(ctrl));

		if (m0 == NULL) {
			m0 = mtail = m;
			mbuf_pkthdr_setlen(m0, mbuf_len(m));
		} else {
			mbuf_setnext(mtail, m);
			mtail = m;
			mbuf_pkthdr_setlen(m0, mbuf_pkthdr_len(m0) + mbuf_len(m));
		}

		/* TODO handle desc->info */

		/* check if this was the last mbuf of the chain */
		if (ctrl & MT_DMA_CTL_LAST_SEC0) {
			ml_enqueue(ml, m0);
			m0 = NULL;
			mtail = NULL;
		}

		idx = (idx + 1) % q->mq_count;

		/* check if more data made it in */
		/* XXX should we actually do that? */
		if (idx == last)
			last = mwx_read(sc, q->mq_regbase + MT_DMA_DMA_IDX);
	}

	/* XXX make sure we don't have half processed data */
	KASSERT(m0 == NULL);

	bus_dmamap_sync(sc->sc_dmat, q->mq_map, 0, q->mq_map->dm_mapsize,
	    BUS_DMASYNC_POSTREAD);

	q->mq_cons = idx;
}

void
mwx_dma_rx_done(struct mwx_softc *sc, struct mwx_queue *q)
{
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();

	uint64_t t0 = mach_absolute_time();
	mwx_dma_rx_dequeue(sc, q, &ml);
	uint64_t t1 = mach_absolute_time();

	if (ml_empty(&ml))
		return;
	if (q == &sc->sc_rxq) {
		gYYPfRxPkts += ml_len(&ml);
		gYYPfRxCalls++;
	}

	mwx_queue_fill(sc, q);	/* TODO what if it fails, run timer? */
	uint64_t t2 = mach_absolute_time();

	mwx_dma_rx_process(sc, &ml);
	if (q == &sc->sc_rxq) {
		gYYPfDeq += t1 - t0;
		gYYPfFill += t2 - t1;
		gYYPfProc += mach_absolute_time() - t2;
	}
}

struct mbuf *
mwx_mcu_alloc_msg(size_t len)
{
	const int headspace = sizeof(struct mwx_mcu_txd);
	struct mbuf *m;

	/* Allocate mbuf with enough space */
	m = MCLGETL(NULL, M_DONTWAIT, len + headspace);
	if (m == NULL) {
		MWX_LOG("mcu_alloc_msg FAIL len=%zu headspace=%d\n", len, headspace);
		return NULL;
	}
	mbuf_setlen(m, len + headspace);
	mbuf_pkthdr_setlen(m, len + headspace);
	m_adj(m, headspace);

	return m;
}

void
mwx_mcu_set_len(struct mbuf *m, void *end)
{
	uint8_t *start = mtod(m, uint8_t *);
	uint8_t *finish = (uint8_t *)end;
	int len = mbuf_pkthdr_len(m), adj;

	KASSERT(start <= finish && finish - start <= len);
	adj = len - (finish - start);
	m_adj(m, -adj);
}

int
mwx_mcu_send_mbuf(struct mwx_softc *sc, uint32_t cmd, struct mbuf *m, int *seqp)
{
	struct mwx_uni_txd *uni_txd;
	struct mwx_mcu_txd *mcu_txd;
	struct mwx_queue *q;
	uint32_t *txd, val;
	int s, rv, txd_len = 0, mcu_cmd = cmd & MCU_CMD_FIELD_ID_MASK;
	int tot_len, len = mbuf_pkthdr_len(m);
	size_t lead;
	uint8_t seq = 0;

	if (cmd == MCU_CMD_FW_SCATTER) {
		q = &sc->sc_txfwdlq;
		goto enqueue;
	}

	seq = ++sc->sc_mcu_seq & 0x0f;
	if (seq == 0)
		seq = ++sc->sc_mcu_seq & 0x0f;

	KASSERT(seq < nitems(sc->sc_mcu_wait));
	txd_len = cmd & MCU_CMD_FIELD_UNI ? sizeof(*uni_txd) : sizeof(*mcu_txd);
	tot_len = txd_len + len;
	lead = m_leadingspace(m);
	if (lead < (size_t)txd_len) {
		static uint32_t rebuild_log_count;
		struct mbuf *n;

		if (rebuild_log_count < 32) {
			MWX_DEV_LOG(sc,
			    "MCU tx mbuf rebuild cmd=0x%08x len=%d lead=%zu txd_len=%d\n",
			    cmd, len, lead, txd_len);
			rebuild_log_count++;
		}
		n = MCLGETL(NULL, M_DONTWAIT, len + txd_len);
		if (n == NULL) {
			m_freem(m);
			return ENOMEM;
		}
		mbuf_setlen(n, len + txd_len);
		mbuf_pkthdr_setlen(n, len + txd_len);
		if (len != 0 &&
		    mbuf_copydata(m, 0, len, mtod(n, uint8_t *) + txd_len) != 0) {
			m_freem(n);
			m_freem(m);
			return EINVAL;
		}
		m_freem(m);
		m = n;
	} else {
		m = m_prepend(m, txd_len, M_DONTWAIT);
		if (m == NULL)
			return ENOMEM;
	}
	txd = mtod(m, uint32_t *);
	memset(txd, 0, txd_len);

	val = (tot_len & MT_TXD0_TX_BYTES_MASK) |
	    MT_TX_TYPE_CMD | MT_TXD0_Q_IDX(MT_TX_MCU_PORT_RX_Q0);
	txd[0] = htole32(val);

	val = MT_TXD1_LONG_FORMAT | MT_HDR_FORMAT_CMD;
	txd[1] = htole32(val);

	if (cmd & MCU_CMD_FIELD_UNI) {
		uni_txd = (struct mwx_uni_txd *)txd;
		uni_txd->len = htole16(tot_len - sizeof(uni_txd->txd));
		uni_txd->option = MCU_CMD_UNI_EXT_ACK;
		uni_txd->cid = htole16(mcu_cmd);
		uni_txd->s2d_index = CMD_S2D_IDX_H2N;
		uni_txd->pkt_type = MCU_PKT_ID;
		uni_txd->seq = seq;
	} else {
		mcu_txd = (struct mwx_mcu_txd *)txd;
		mcu_txd->len = htole16(tot_len - sizeof(uni_txd->txd));
		mcu_txd->pq_id = htole16(MCU_PQ_ID(MT_TX_PORT_IDX_MCU,
			MT_TX_MCU_PORT_RX_Q0));
		mcu_txd->pkt_type = MCU_PKT_ID;
		mcu_txd->seq = seq;
		mcu_txd->cid = mcu_cmd;
		mcu_txd->s2d_index = CMD_S2D_IDX_H2N;
		mcu_txd->ext_cid = MCU_GET_EXT_CMD(cmd);

		if (mcu_txd->ext_cid || (cmd & MCU_CMD_FIELD_CE)) {
			if (cmd & MCU_CMD_FIELD_QUERY)
				mcu_txd->set_query = MCU_Q_QUERY;
			else
				mcu_txd->set_query = MCU_Q_SET;
			mcu_txd->ext_cid_ack = !!mcu_txd->ext_cid;
		} else {
			mcu_txd->set_query = MCU_Q_NA;
		}
	}

	if (seqp != NULL) {
		memset(&sc->sc_mcu_wait[seq], 0, sizeof(sc->sc_mcu_wait[0]));
		sc->sc_mcu_wait[seq].mcu_cmd = cmd;
		*seqp = seq;
	}
	q = &sc->sc_txmcuq;
enqueue:

	if (cmd != MCU_CMD_FW_SCATTER) {
		static uint32_t mcu_tx_log_count;

		if (mcu_tx_log_count < 96) {
			MWX_DEV_LOG(sc,
			    "mcu tx cmd=0x%08x seq=%u len=%d txd_len=%d q_reg=0x%08x\n",
			    cmd, seq, len, txd_len, q->mq_regbase);
			mcu_tx_log_count++;
		}
#ifdef MWX_PACKET_HEXDUMP
		pkt_hex_dump(m);
#endif
	}

	s = splnet();
	while (1) {
		rv = mwx_dma_tx_enqueue(sc, q, m);
		if (rv != EBUSY)
			break;
		q->mq_wakeme = 1;
		if (mwx_on_workloop()) {
			(void)mwx_intr(sc);
			IODelay(500);
		} else
			tsleep_nsec(q, 0, "mwxq", MSEC_TO_NSEC(100));
	}
	splx(s);
	if (rv != 0) {
		if (seqp != NULL)
			memset(&sc->sc_mcu_wait[seq], 0,
			    sizeof(sc->sc_mcu_wait[0]));
		m_freem(m);
	}
	return rv;
}

int
mwx_mcu_send_msg(struct mwx_softc *sc, uint32_t cmd, void *data, size_t len,
    int *seqp)
{
	struct mbuf *m;

	m = mwx_mcu_alloc_msg(len);
	if (m == NULL)
		return ENOMEM;

	if (len != 0)
		memcpy(mtod(m, caddr_t), data, len);

	return mwx_mcu_send_mbuf(sc, cmd, m, seqp);
}

int
mwx_mcu_send_wait(struct mwx_softc *sc, uint32_t cmd, void *data, size_t len)
{
	int rv, seq;

	rv = mwx_mcu_send_msg(sc, cmd, data, len, &seq);
	if (rv != 0)
		return rv;
	return mwx_mcu_wait_resp_int(sc, cmd, seq, NULL);
}

int
mwx_mcu_send_mbuf_wait(struct mwx_softc *sc, uint32_t cmd, struct mbuf *m)
{
	int rv, seq;

	rv = mwx_mcu_send_mbuf(sc, cmd, m, &seq);
	if (rv != 0)
		return rv;
	return mwx_mcu_wait_resp_int(sc, cmd, seq, NULL);
}

void
mwx_mcu_rx_event(struct mwx_softc *sc, struct mbuf *m)
{
	struct mwx_mcu_rxd *rxd;
	uint32_t cmd, mcu_int = 0;
	uint8_t rxd_eid, rxd_ext_eid, rxd_seq;
	int len, rxd_size;

	rxd_size = MT7921_MCU_RXD_SIZE;
	if ((m = m_pullup(m, sizeof(*rxd) + rxd_size)) == NULL)
		return;
	m_adj(m, rxd_size);
	rxd = mtod(m, struct mwx_mcu_rxd *);

	if (rxd->ext_eid == MCU_EXT_EVENT_RATE_REPORT) {
		smtw_trace("%s: MCU_EXT_EVENT_RATE_REPORT COMMAND\n", DEVNAME(sc));
		m_freem(m);
		return;
	}

	len = le16toh(rxd->len);
	/* make sure all the data is in one mbuf */
	if ((m = m_pullup(m, len)) == NULL) {
		smtw_trace("%s: mwx_mcu_rx_event m_pullup failed\n", DEVNAME(sc));
		return;
	}
	/* refetch after pullup */
	rxd = mtod(m, struct mwx_mcu_rxd *);
	rxd_eid = rxd->eid;
	rxd_ext_eid = rxd->ext_eid;
	rxd_seq = rxd->seq;
	m_adj(m, sizeof(*rxd));

	switch (rxd_eid) {
	case MCU_EVENT_DBG_MSG: {
		/* R9T: firmware gunlugu (metin) - TX takilmasini anlamak icin */
		static uint32_t fwlog_n;
		if (fwlog_n < 120 && mbuf_len(m) > 0) {
			char buf[160]; size_t n = mbuf_len(m), k, o = 0; const uint8_t *d = mtod(m, const uint8_t *);
			if (n > 150) n = 150;
			for (k = 0; k < n && o < sizeof(buf) - 1; k++)
				buf[o++] = (d[k] >= 32 && d[k] < 127) ? (char)d[k] : '.';
			buf[o] = 0;
			fwlog_n++;
			MWX_DEV_LOG(sc, "FWLOG[%u] %s\n", fwlog_n, buf);
		}
		m_freem(m);
		return;
	}
	case MCU_EVENT_SCHED_SCAN_DONE:
	case MCU_EVENT_SCAN_DONE:
		mt7921_mcu_scan_event(sc, m);
		break;
#if 0
	case MCU_EVENT_BSS_BEACON_LOSS:
		mt7921_mcu_connection_loss_event(dev, skb);
		break;
	case MCU_EVENT_BSS_ABSENCE:
		mt7921_mcu_bss_event(dev, skb);
		break;
	case MCU_EVENT_DBG_MSG:
		mt7921_mcu_debug_msg_event(dev, skb);
		break;
#endif
	case MCU_EVENT_COREDUMP:
		/* it makes little sense to write the coredump down */
		if (!sc->sc_resetting)
			smtw_trace("%s: coredump event\n", DEVNAME(sc));
		mwx_reset(sc);
		break;
	case MCU_EVENT_LP_INFO:
		mt7921_mcu_low_power_event(sc, m);
		break;
	case MCU_EVENT_TX_DONE:
		mt7921_mcu_tx_done_event(sc, m);
		break;
	case 0x6:
		smtw_trace("%s: MAGIC COMMAND\n", DEVNAME(sc));
	default:
		if (rxd_seq == 0 || rxd_seq >= nitems(sc->sc_mcu_wait)) {
			smtw_trace("%s: mcu rx bad seq %x\n", DEVNAME(sc),
			    rxd_seq);
			break;
		}

		cmd = sc->sc_mcu_wait[rxd_seq].mcu_cmd;

		if (cmd == MCU_CMD_PATCH_SEM_CONTROL ||
		    cmd == MCU_CMD_PATCH_FINISH_REQ) {
			mcu_int = rxd_ext_eid;
			MWX_DEV_LOG(sc, "MCU patch response cmd=0x%08x seq=%u status=0x%x\n",
			    cmd, rxd_seq, mcu_int);
		} else if (cmd == MCU_EXT_CMD_THERMAL_CTRL) {
			if (mbuf_len(m) < sizeof(uint32_t) * 2)
				break;
			mcu_int = le32toh(mtod(m, uint32_t *)[1]);
		} else if (cmd == MCU_EXT_CMD_EFUSE_ACCESS) {
			//ret = mt7921_mcu_parse_eeprom(sc, m);
			smtw_trace("%s: mcu resp no handled yet\n", DEVNAME(sc));
		} else if (cmd == MCU_UNI_CMD_DEV_INFO_UPDATE ||
		    cmd == MCU_UNI_CMD_BSS_INFO_UPDATE ||
		    cmd == MCU_UNI_CMD_STA_REC_UPDATE ||
		    cmd == MCU_UNI_CMD_HIF_CTRL ||
		    cmd == MCU_UNI_CMD_OFFLOAD ||
		    cmd == MCU_UNI_CMD_SUSPEND) {
			struct mt7921_mcu_uni_event *event;

			if (mbuf_len(m) < sizeof(*event))
				break;
			event = mtod(m, struct mt7921_mcu_uni_event *);
			mcu_int = le32toh(event->status);
		} else if (cmd == MCU_CE_QUERY_REG_READ) {
			struct mt7921_mcu_reg_event *event;

			if (mbuf_len(m) < sizeof(*event))
				break;
			event = mtod(m, struct mt7921_mcu_reg_event *);
			mcu_int = le32toh(event->val);
		}

		sc->sc_mcu_wait[rxd_seq].mcu_int = mcu_int;
		sc->sc_mcu_wait[rxd_seq].mcu_m = m;
		wakeup(&sc->sc_mcu_wait[rxd_seq]);
		return;
	}

	m_freem(m);
}


/*
 * When we run on the workloop thread (command gate: ifconfig up, scan, ...),
 * msleep() would block the workloop and the interrupt event source (same
 * workloop) can never run. Service the interrupt handler ourselves instead.
 */
static int
mwx_wait_slot_poll(struct mwx_softc *sc, int seq, int timeout_ms)
{
	int i, n = timeout_ms * 4;

	for (i = 0; i < n; i++) {
		(void)mwx_intr(sc);
		if (sc->sc_mcu_wait[seq].mcu_m != NULL)
			return 0;
		IODelay(250);
	}
	return EWOULDBLOCK;
}

static int
mwx_diag_timeout(struct mwx_softc *sc, uint32_t cmd, int seq)
{
	uint32_t ev0 = sc->sc_rx_pkt_event;
	struct mwx_queue *qs[4] = { &sc->sc_rxmcuq, &sc->sc_rxfwdlq, &sc->sc_txmcuq, &sc->sc_txfwdlq };
	const char *nm[4] = { "rxmcu", "rxfwdl", "txmcu", "txfwdl" };
	int i;

	MWX_DEV_LOG(sc, "TIMEOUT intr_calls=%u body=%u casfail=%u busy=%u poll_fires=%u waitpoll=%u waitsleep=%u onwl=%d pcie_mac_ena=0x%08x\n",
	    (unsigned)gMwxIntrCalls, (unsigned)gMwxBodyCalls, (unsigned)gMwxCasFails, (unsigned)gMwxIntrBusy,
	    (unsigned)gMwxPollFires, (unsigned)gMwxWaitPoll, (unsigned)gMwxWaitSleep, (int)mwx_on_workloop(),
	    mwx_read(sc, MT_PCIE_MAC_INT_ENABLE));
	MWX_DEV_LOG(sc, "TIMEOUT cmd=%x seq=%d int_sta=0x%08x int_ena=0x%08x glo=0x%08x mask=0x%08x\n",
	    cmd, seq, mwx_read(sc, MT_WFDMA0_HOST_INT_STA),
	    mwx_read(sc, MT_WFDMA0_HOST_INT_ENA), mwx_read(sc, MT_WFDMA0_GLO_CFG),
	    sc->sc_intr_mask);
	for (i = 0; i < 4; i++)
		MWX_DEV_LOG(sc, "TIMEOUT q=%s reg=0x%x cpu=%u dma=%u prod=%d cons=%d\n", nm[i],
		    qs[i]->mq_regbase,
		    mwx_read(sc, qs[i]->mq_regbase + MT_DMA_CPU_IDX),
		    mwx_read(sc, qs[i]->mq_regbase + MT_DMA_DMA_IDX),
		    (int)qs[i]->mq_prod, (int)qs[i]->mq_cons);
	mwx_dma_rx_done(sc, &sc->sc_rxmcuq);
	mwx_dma_rx_done(sc, &sc->sc_rxfwdlq);
	MWX_DEV_LOG(sc, "TIMEOUT poll events %u -> %u\n", ev0, sc->sc_rx_pkt_event);
	return sc->sc_rx_pkt_event != ev0;
}

int
mwx_mcu_wait_resp_int(struct mwx_softc *sc, uint32_t cmd, int seq,
    uint32_t *val)
{
	int rv;

	KASSERT(seq < nitems(sc->sc_mcu_wait));

	if (mwx_on_workloop()) {
		gMwxWaitPoll++;
		rv = mwx_wait_slot_poll(sc, seq, 3000);
	} else {
		gMwxWaitSleep++;
		rv = tsleep_nsec(&sc->sc_mcu_wait[seq], 0, "mwxwait", SEC_TO_NSEC(3));
	}
	if (rv != 0) {
		if (mwx_diag_timeout(sc, cmd, seq)) {
			MWX_DEV_LOG(sc, "TIMEOUT recovered by polling cmd=%x\n", cmd);
			rv = 0;
		} else {
			MWX_DEV_LOG(sc, "command %x timeout (no rx)\n", cmd);
			mwx_reset(sc);
			return rv;
		}
	}

	if (sc->sc_mcu_wait[seq].mcu_m != NULL) {
		m_freem(sc->sc_mcu_wait[seq].mcu_m);
		sc->sc_mcu_wait[seq].mcu_m = NULL;
	}
	if (val != NULL)
		*val = sc->sc_mcu_wait[seq].mcu_int;
	return 0;
}

int
mwx_mcu_wait_resp_msg(struct mwx_softc *sc, uint32_t cmd, int seq,
    struct mbuf **mp)
{
	int rv;

	KASSERT(seq < nitems(sc->sc_mcu_wait));

	if (mwx_on_workloop()) {
		gMwxWaitPoll++;
		rv = mwx_wait_slot_poll(sc, seq, 3000);
	} else {
		gMwxWaitSleep++;
		rv = tsleep_nsec(&sc->sc_mcu_wait[seq], 0, "mwxwait", SEC_TO_NSEC(3));
	}
	if (rv != 0) {
		if (mwx_diag_timeout(sc, cmd, seq)) {
			MWX_DEV_LOG(sc, "TIMEOUT recovered by polling cmd=%x\n", cmd);
			rv = 0;
		} else {
			MWX_DEV_LOG(sc, "command %x timeout (no rx)\n", cmd);
			mwx_reset(sc);
			return rv;
		}
	}
	if (sc->sc_mcu_wait[seq].mcu_m == NULL) {
		smtw_trace("%s: command response missing\n", DEVNAME(sc));
		return ENOENT;
	}
	if (mp != NULL)
		*mp = sc->sc_mcu_wait[seq].mcu_m;
	else
		m_freem(sc->sc_mcu_wait[seq].mcu_m);
	sc->sc_mcu_wait[seq].mcu_m = NULL;
	return 0;
}

int
mwx_dma_disable(struct mwx_softc *sc, int force)
{
	int rv;

	/* disable WFDMA0 */
	mwx_clear(sc, MT_WFDMA0_GLO_CFG,
	    MT_WFDMA0_GLO_CFG_TX_DMA_EN | MT_WFDMA0_GLO_CFG_RX_DMA_EN |
	    MT_WFDMA0_GLO_CFG_CSR_DISP_BASE_PTR_CHAIN_EN |
	    MT_WFDMA0_GLO_CFG_OMIT_TX_INFO |
	    MT_WFDMA0_GLO_CFG_OMIT_RX_INFO |
	    MT_WFDMA0_GLO_CFG_OMIT_RX_INFO_PFET2);

	rv = mwx_poll(sc, MT_WFDMA0_GLO_CFG, 0,
	    MT_WFDMA0_GLO_CFG_TX_DMA_BUSY | MT_WFDMA0_GLO_CFG_RX_DMA_BUSY, 100);
	if (!force && rv != 0)
		return rv;

	/* disable DMASHDL and set bypass */
	mwx_clear(sc, MT_WFDMA0_GLO_CFG_EXT0, MT_WFDMA0_CSR_TX_DMASHDL_ENABLE);
	mwx_set(sc, MT_DMASHDL_SW_CONTROL, MT_DMASHDL_DMASHDL_BYPASS);

	if (force) {
		/* reset */
		mwx_clear(sc, MT_WFDMA0_RST, MT_WFDMA0_RST_DMASHDL_ALL_RST |
		    MT_WFDMA0_RST_LOGIC_RST);
		mwx_set(sc, MT_WFDMA0_RST, MT_WFDMA0_RST_DMASHDL_ALL_RST |
		    MT_WFDMA0_RST_LOGIC_RST);
		delay(20);
	}

	return 0;
}

void
mwx_dma_enable(struct mwx_softc *sc)
{
#define PREFETCH(base, depth)   ((base) << 16 | (depth))
	/* configure perfetch settings */
	mwx_write(sc, MT_WFDMA0_RX_RING0_EXT_CTRL, PREFETCH(0x0, 0x4));
	mwx_write(sc, MT_WFDMA0_RX_RING2_EXT_CTRL, PREFETCH(0x40, 0x4));
	mwx_write(sc, MT_WFDMA0_RX_RING3_EXT_CTRL, PREFETCH(0x80, 0x4));
	mwx_write(sc, MT_WFDMA0_RX_RING4_EXT_CTRL, PREFETCH(0xc0, 0x4));
	mwx_write(sc, MT_WFDMA0_RX_RING5_EXT_CTRL, PREFETCH(0x100, 0x4));

	mwx_write(sc, MT_WFDMA0_TX_RING0_EXT_CTRL, PREFETCH(0x140, 0x4));
	mwx_write(sc, MT_WFDMA0_TX_RING1_EXT_CTRL, PREFETCH(0x180, 0x4));
	mwx_write(sc, MT_WFDMA0_TX_RING2_EXT_CTRL, PREFETCH(0x1c0, 0x4));
	mwx_write(sc, MT_WFDMA0_TX_RING3_EXT_CTRL, PREFETCH(0x200, 0x4));
	mwx_write(sc, MT_WFDMA0_TX_RING4_EXT_CTRL, PREFETCH(0x240, 0x4));
	mwx_write(sc, MT_WFDMA0_TX_RING5_EXT_CTRL, PREFETCH(0x280, 0x4));
	mwx_write(sc, MT_WFDMA0_TX_RING6_EXT_CTRL, PREFETCH(0x2c0, 0x4));
	mwx_write(sc, MT_WFDMA0_TX_RING16_EXT_CTRL, PREFETCH(0x340, 0x4));
	mwx_write(sc, MT_WFDMA0_TX_RING17_EXT_CTRL, PREFETCH(0x380, 0x4));

	/* reset dma idx */
	mwx_write(sc, MT_WFDMA0_RST_DTX_PTR, ~0);

	/* configure delay interrupt */
	mwx_write(sc, MT_WFDMA0_PRI_DLY_INT_CFG0, 0);

	mwx_set(sc, MT_WFDMA0_GLO_CFG,
	    MT_WFDMA0_GLO_CFG_TX_WB_DDONE |
	    MT_WFDMA0_GLO_CFG_FIFO_LITTLE_ENDIAN |
	    MT_WFDMA0_GLO_CFG_CLK_GAT_DIS |
	    MT_WFDMA0_GLO_CFG_OMIT_TX_INFO |
	    MT_WFDMA0_GLO_CFG_DMA_SIZE(3) |
	    MT_WFDMA0_GLO_CFG_FIFO_DIS_CHECK |
	    MT_WFDMA0_GLO_CFG_RX_WB_DDONE |
	    MT_WFDMA0_GLO_CFG_CSR_DISP_BASE_PTR_CHAIN_EN |
	    MT_WFDMA0_GLO_CFG_OMIT_RX_INFO_PFET2);

	mwx_barrier(sc);
	mwx_set(sc, MT_WFDMA0_GLO_CFG,
	    MT_WFDMA0_GLO_CFG_TX_DMA_EN | MT_WFDMA0_GLO_CFG_RX_DMA_EN);

	mwx_set(sc, MT_WFDMA_DUMMY_CR, MT_WFDMA_NEED_REINIT);

	/* enable interrupts for TX/RX rings */
	mwx_write(sc, MT_WFDMA0_HOST_INT_ENA, sc->sc_intr_mask);
	mwx_set(sc, MT_MCU2HOST_SW_INT_ENA, MT_MCU_CMD_WAKE_RX_PCIE);
	mwx_write(sc, MT_PCIE_MAC_INT_ENABLE, 0xff);
}

int
mwx_mcu_fw_pmctrl(struct mwx_softc *sc)
{
	int i;

	for (i = 0; i < MWX_MCU_INIT_RETRY_COUNT; i++) {
		mwx_write(sc, MT_CONN_ON_LPCTL, PCIE_LPCR_HOST_SET_OWN);
		if (mwx_poll(sc, MT_CONN_ON_LPCTL, PCIE_LPCR_HOST_OWN_SYNC,
		    4, 50) == 0)
			break;
	}

	if (i == MWX_MCU_INIT_RETRY_COUNT) {
		smtw_trace("%s: firmware own failed\n", DEVNAME(sc));
		return EIO;
	}

	return 0;
}

int
mwx_mcu_drv_pmctrl(struct mwx_softc *sc)
{
	int i;

	for (i = 0; i < MWX_MCU_INIT_RETRY_COUNT; i++) {
		mwx_write(sc, MT_CONN_ON_LPCTL, PCIE_LPCR_HOST_CLR_OWN);
		delay(3000);
		if (mwx_poll(sc, MT_CONN_ON_LPCTL, 0,
		    PCIE_LPCR_HOST_OWN_SYNC, 50) == 0)
			break;
	}

	if (i == MWX_MCU_INIT_RETRY_COUNT) {
		smtw_trace("%s: driver own failed\n", DEVNAME(sc));
		return EIO;
	}

	return 0;
}

int
mwx_wfsys_reset(struct mwx_softc *sc)
{
	DPRINTF("%s: WFSYS reset\n", DEVNAME(sc));

	mwx_clear(sc, MT_WFSYS_SW_RST_B, WFSYS_SW_RST_B);
	delay(50 * 1000);
	mwx_set(sc, MT_WFSYS_SW_RST_B, WFSYS_SW_RST_B);

	return mwx_poll(sc, MT_WFSYS_SW_RST_B, WFSYS_SW_INIT_DONE,
	    WFSYS_SW_INIT_DONE, 500);
}

/*
 * To be honest this is ridiculous.
 */
uint32_t
mwx_reg_addr(struct mwx_softc *sc, uint32_t reg)
{
	static const struct {
		uint32_t phys;
		uint32_t mapped;
		uint32_t size;
	} fixed_map[] = {
	{ 0x820d0000, 0x30000, 0x10000 }, /* WF_LMAC_TOP (WF_WTBLON) */
	{ 0x820ed000, 0x24800, 0x00800 }, /* WF_LMAC_TOP BN0 (WF_MIB) */
	{ 0x820e4000, 0x21000, 0x00400 }, /* WF_LMAC_TOP BN0 (WF_TMAC) */
	{ 0x820e7000, 0x21e00, 0x00200 }, /* WF_LMAC_TOP BN0 (WF_DMA) */
	{ 0x820eb000, 0x24200, 0x00400 }, /* WF_LMAC_TOP BN0 (WF_LPON) */
	{ 0x820e2000, 0x20800, 0x00400 }, /* WF_LMAC_TOP BN0 (WF_AGG) */
	{ 0x820e3000, 0x20c00, 0x00400 }, /* WF_LMAC_TOP BN0 (WF_ARB) */
	{ 0x820e5000, 0x21400, 0x00800 }, /* WF_LMAC_TOP BN0 (WF_RMAC) */
	{ 0x00400000, 0x80000, 0x10000 }, /* WF_MCU_SYSRAM */
	{ 0x00410000, 0x90000, 0x10000 }, /* WF_MCU_SYSRAM (conf register) */
	{ 0x40000000, 0x70000, 0x10000 }, /* WF_UMAC_SYSRAM */
	{ 0x54000000, 0x02000, 0x01000 }, /* WFDMA PCIE0 MCU DMA0 */
	{ 0x55000000, 0x03000, 0x01000 }, /* WFDMA PCIE0 MCU DMA1 */
	{ 0x58000000, 0x06000, 0x01000 }, /* WFDMA PCIE1 MCU DMA0 (MEM_DMA) */
	{ 0x59000000, 0x07000, 0x01000 }, /* WFDMA PCIE1 MCU DMA1 */
	{ 0x7c000000, 0xf0000, 0x10000 }, /* CONN_INFRA */
	{ 0x7c020000, 0xd0000, 0x10000 }, /* CONN_INFRA, WFDMA */
	{ 0x7c060000, 0xe0000, 0x10000 }, /* CONN_INFRA, conn_host_csr_top */
	{ 0x80020000, 0xb0000, 0x10000 }, /* WF_TOP_MISC_OFF */
	{ 0x81020000, 0xc0000, 0x10000 }, /* WF_TOP_MISC_ON */
	{ 0x820c0000, 0x08000, 0x04000 }, /* WF_UMAC_TOP (PLE) */
	{ 0x820c8000, 0x0c000, 0x02000 }, /* WF_UMAC_TOP (PSE) */
	{ 0x820cc000, 0x0e000, 0x01000 }, /* WF_UMAC_TOP (PP) */
	{ 0x820cd000, 0x0f000, 0x01000 }, /* WF_MDP_TOP */
	{ 0x820ce000, 0x21c00, 0x00200 }, /* WF_LMAC_TOP (WF_SEC) */
	{ 0x820cf000, 0x22000, 0x01000 }, /* WF_LMAC_TOP (WF_PF) */
	{ 0x820e0000, 0x20000, 0x00400 }, /* WF_LMAC_TOP BN0 (WF_CFG) */
	{ 0x820e1000, 0x20400, 0x00200 }, /* WF_LMAC_TOP BN0 (WF_TRB) */
	{ 0x820e9000, 0x23400, 0x00200 }, /* WF_LMAC_TOP BN0 (WF_WTBLOFF) */
	{ 0x820ea000, 0x24000, 0x00200 }, /* WF_LMAC_TOP BN0 (WF_ETBF) */
	{ 0x820ec000, 0x24600, 0x00200 }, /* WF_LMAC_TOP BN0 (WF_INT) */
	{ 0x820f0000, 0xa0000, 0x00400 }, /* WF_LMAC_TOP BN1 (WF_CFG) */
	{ 0x820f1000, 0xa0600, 0x00200 }, /* WF_LMAC_TOP BN1 (WF_TRB) */
	{ 0x820f2000, 0xa0800, 0x00400 }, /* WF_LMAC_TOP BN1 (WF_AGG) */
	{ 0x820f3000, 0xa0c00, 0x00400 }, /* WF_LMAC_TOP BN1 (WF_ARB) */
	{ 0x820f4000, 0xa1000, 0x00400 }, /* WF_LMAC_TOP BN1 (WF_TMAC) */
	{ 0x820f5000, 0xa1400, 0x00800 }, /* WF_LMAC_TOP BN1 (WF_RMAC) */
	{ 0x820f7000, 0xa1e00, 0x00200 }, /* WF_LMAC_TOP BN1 (WF_DMA) */
	{ 0x820f9000, 0xa3400, 0x00200 }, /* WF_LMAC_TOP BN1 (WF_WTBLOFF) */
	{ 0x820fa000, 0xa4000, 0x00200 }, /* WF_LMAC_TOP BN1 (WF_ETBF) */
	{ 0x820fb000, 0xa4200, 0x00400 }, /* WF_LMAC_TOP BN1 (WF_LPON) */
	{ 0x820fc000, 0xa4600, 0x00200 }, /* WF_LMAC_TOP BN1 (WF_INT) */
	{ 0x820fd000, 0xa4800, 0x00800 }, /* WF_LMAC_TOP BN1 (WF_MIB) */
	};
	int i;

	if (reg < 0x100000)
		return reg;

	for (i = 0; i < nitems(fixed_map); i++) {
		uint32_t ofs;

		if (reg < fixed_map[i].phys)
			continue;

		ofs = reg - fixed_map[i].phys;
		if (ofs > fixed_map[i].size)
			continue;

		return fixed_map[i].mapped + ofs;
	}

	if ((reg >= 0x18000000 && reg < 0x18c00000) ||
	    (reg >= 0x70000000 && reg < 0x78000000) ||
	    (reg >= 0x7c000000 && reg < 0x7c400000))
		return mt7921_map_reg_l1(sc, reg);

	panic("%s: Access to currently unsupported address %08x\n",
	    DEVNAME(sc), reg);
}

int
mwx_init_hardware(struct mwx_softc *sc)
{
	int rv;

	MWX_DEV_LOG(sc, "hardware init start\n");
	/* reset dma */
	rv = mwx_dma_reset(sc, 1);
	if (rv != 0) {
		MWX_DEV_LOG(sc, "hardware init dma reset failed error=%d\n", rv);
		return rv;
	}
	MWX_DEV_LOG(sc, "hardware init dma reset ok\n");

	/*
	 * force firmware operation mode into normal state,
	 * which should be set before firmware download stage.
	 */
	mwx_write(sc, MT_SWDEF_MODE, MT_SWDEF_NORMAL_MODE);
	mwx_barrier(sc);
	MWX_DEV_LOG(sc, "hardware init normal firmware mode set\n");

	rv = mt7921_mcu_init(sc);
	if (rv != 0) {
		MWX_DEV_LOG(sc, "hardware init MCU init failed error=%d\n", rv);
		goto fail;
	}
	MWX_DEV_LOG(sc, "hardware init MCU init ok\n");
	/* TODO override eeprom for systems with FDT */
	rv = mt7921_mcu_set_eeprom(sc);
	if (rv != 0) {
		MWX_DEV_LOG(sc, "hardware init EEPROM setup failed error=%d\n", rv);
		goto fail;
	}
	MWX_DEV_LOG(sc, "hardware init EEPROM setup ok\n");
	mwx_mac_init(sc);

	sc->sc_vif.vif_mn.wcid = MWX_WTBL_RESERVED;

	/* MAYBE alloc beacon and mgmt frame wcid 0 here */

	MWX_DEV_LOG(sc, "hardware init complete\n");
	return 0;

 fail:
	/* reset dma */
	MWX_DEV_LOG(sc, "hardware init recovery dma reset start\n");
	rv = mwx_dma_reset(sc, 1);
	if (rv != 0) {
		MWX_DEV_LOG(sc, "hardware init recovery dma reset failed error=%d\n",
		    rv);
		return rv;
	}
	return EAGAIN;
}

int
mt7921_mcu_init(struct mwx_softc *sc)
{
	int rv;

	MWX_DEV_LOG(sc, "MCU init start\n");
	/* this read is needed to make interrupts work */
	(void) mwx_read(sc, MT_TOP_LPCR_HOST_BAND0);
	mwx_write(sc, MT_TOP_LPCR_HOST_BAND0, MT_TOP_LPCR_HOST_DRV_OWN);
	if (mwx_poll(sc, MT_TOP_LPCR_HOST_BAND0, 0, MT_TOP_LPCR_HOST_FW_OWN,
	    5000) != 0) {
	    MWX_DEV_LOG(sc, "MCU init timeout for driver own\n");
	    return EIO;
	}
	MWX_DEV_LOG(sc, "MCU init driver own ok\n");

	mwx_set(sc, MT_PCIE_MAC_PM, MT_PCIE_MAC_PM_L0S_DIS);

	if ((rv = mwx_load_firmware(sc)) != 0) {
		MWX_DEV_LOG(sc, "MCU init firmware load failed error=%d\n", rv);
		return rv;
	}
	MWX_DEV_LOG(sc, "MCU init firmware load ok\n");

	if ((rv = mt7921_mcu_get_nic_capability(sc)) != 0) {
		MWX_DEV_LOG(sc, "MCU init NIC capability failed error=%d\n", rv);
		return rv;
	}
	MWX_DEV_LOG(sc, "MCU init NIC capability ok\n");
	if ((rv = mt7921_mcu_fw_log_2_host(sc, 1)) != 0) {
		MWX_DEV_LOG(sc, "MCU init firmware log enable failed error=%d\n",
		    rv);
		return rv;
	}

	/* TODO mark MCU running */

	MWX_DEV_LOG(sc, "MCU init complete\n");
	return 0;
}

static inline uint32_t
mwx_get_data_mode(struct mwx_softc *sc, uint32_t info)
{
	uint32_t mode = DL_MODE_NEED_RSP;

	if (info == PATCH_SEC_NOT_SUPPORT)
		return mode;
	switch (info & PATCH_SEC_ENC_TYPE_MASK) {
	case PATCH_SEC_ENC_TYPE_PLAIN:
		break;
	case PATCH_SEC_ENC_TYPE_AES:
		mode |= DL_MODE_ENCRYPT;
		mode |= (info << DL_MODE_KEY_IDX_SHIFT) & DL_MODE_KEY_IDX_MASK;
		mode |= DL_MODE_RESET_SEC_IV;
		break;
	case PATCH_SEC_ENC_TYPE_SCRAMBLE:
		mode |= DL_MODE_ENCRYPT;
		mode |= DL_CONFIG_ENCRY_MODE_SEL;
		mode |= DL_MODE_RESET_SEC_IV;
		break;
	default:
		smtw_trace("%s: encryption type not supported\n", DEVNAME(sc));
	}
	return mode;
}

static inline uint32_t
mwx_mcu_gen_dl_mode(uint8_t feature_set)
{
	uint32_t ret = DL_MODE_NEED_RSP;

	if (feature_set & FW_FEATURE_SET_ENCRYPT)
		ret |= (DL_MODE_ENCRYPT | DL_MODE_RESET_SEC_IV);
	if (feature_set & FW_FEATURE_ENCRY_MODE)
		ret |= DL_CONFIG_ENCRY_MODE_SEL;

	/* FW_FEATURE_SET_KEY_IDX_MASK == DL_MODE_KEY_IDX_MASK */
	ret |= feature_set & FW_FEATURE_SET_KEY_IDX_MASK;

	return ret;
}


int
mwx_load_firmware(struct mwx_softc *sc)
{
	struct mwx_patch_hdr *hdr;
	struct mwx_fw_trailer *fwhdr;
	const char *rompatch, *fw;
	u_char *buf = NULL, *fwbuf = NULL, *dl;
	size_t buflen = 0, fwlen = 0, offset = 0;
	uint32_t reg, n_region, override = 0, option = 0;
	int i, rv, sem;

	MWX_DEV_LOG(sc, "firmware load start hwtype=%s\n",
	    sc->sc_hwtype == MWX_HW_MT7922 ? "MT7922" : "MT7921");
	reg = mwx_read(sc, MT_CONN_ON_MISC) & MT_TOP_MISC2_FW_N9_RDY;
	if (reg != 0) {
		MWX_DEV_LOG(sc, "firmware already downloaded status=0x%08x\n", reg);
		return 0;
	}

	switch (sc->sc_hwtype) {
	case MWX_HW_MT7921:
		rompatch = MT7921_ROM_PATCH;
		fw = MT7921_FIRMWARE_WM;
		break;
	case MWX_HW_MT7922:
		rompatch = MT7922_ROM_PATCH;
		fw = MT7922_FIRMWARE_WM;
		break;
	}
	MWX_DEV_LOG(sc, "firmware selected patch=%s wm=%s\n", rompatch, fw);

	if ((rv = loadfirmware(rompatch, &buf, &buflen)) != 0) {
		MWX_DEV_LOG(sc, "patch firmware load failed name=%s error=%d\n",
		    rompatch, rv);
		return rv;
	}
	if ((rv = loadfirmware(fw, &fwbuf, &fwlen)) != 0) {
		MWX_DEV_LOG(sc, "WM firmware load failed name=%s error=%d\n",
		    fw, rv);
		free(buf, M_DEVBUF, buflen);
		return rv;
	}
	MWX_DEV_LOG(sc, "firmware blobs loaded patch_size=%zu wm_size=%zu\n",
	    buflen, fwlen);

	rv = mwx_mcu_patch_sem_ctrl(sc, 1);
	if (rv != 0) {
		MWX_DEV_LOG(sc, "patch semaphore acquire failed error=%d\n", rv);
		goto fail;
	}
	MWX_DEV_LOG(sc, "patch semaphore acquired\n");

	if (buflen < sizeof(*hdr)) {
		MWX_DEV_LOG(sc, "invalid patch firmware size=%zu min=%zu\n",
		    buflen, sizeof(*hdr));
		rv = EINVAL;
		goto out;
	}
	hdr = (struct mwx_patch_hdr *)buf;
	n_region = be32toh(hdr->desc.n_region);
	if (buflen < sizeof(*hdr) + n_region * sizeof(struct mwx_patch_sec)) {
		MWX_DEV_LOG(sc, "invalid patch firmware short header size=%zu regions=%u\n",
		    buflen, n_region);
		rv = EINVAL;
		goto out;
	}
	MWX_DEV_LOG(sc, "patch header hw_sw=0x%x patch_ver=0x%x regions=%u build=%.15s\n",
	    be32toh(hdr->hw_sw_ver), be32toh(hdr->patch_ver),
	    n_region, hdr->build_date);

	for (i = 0; i < n_region; i++) {
		struct mwx_patch_sec *sec;
		uint32_t len, addr, mode, sec_info;

		sec = (struct mwx_patch_sec *)(buf + sizeof(*hdr) +
		    i * sizeof(*sec));
		if ((be32toh(sec->type) & PATCH_SEC_TYPE_MASK) !=
		    PATCH_SEC_TYPE_INFO) {
			MWX_DEV_LOG(sc, "invalid patch region index=%d type=0x%x\n",
			    i, be32toh(sec->type));
			rv = EINVAL;
			goto out;
		}

		addr = be32toh(sec->info.addr);
		len = be32toh(sec->info.len);
		dl = buf + be32toh(sec->offs);
		sec_info = be32toh(sec->info.sec_key_idx);
		mode = mwx_get_data_mode(sc, sec_info);
		MWX_DEV_LOG(sc, "patch region index=%d addr=0x%08x len=%u mode=0x%08x sec=0x%08x\n",
		    i, addr, len, mode, sec_info);

		if (dl + len > buf + buflen) {
			MWX_DEV_LOG(sc, "patch region index=%d exceeds payload len=%u\n",
			    i, len);
			rv = EINVAL;
			goto out;
		}

		rv = mwx_mcu_init_download(sc, addr, len, mode);
		if (rv != 0) {
			MWX_DEV_LOG(sc, "patch download request failed index=%d error=%d\n",
			    i, rv);
			goto out;
		}
		rv = mwx_mcu_send_firmware(sc, MCU_CMD_FW_SCATTER,
		    dl, len, 4096);
		if (rv != 0) {
			MWX_DEV_LOG(sc, "patch send failed index=%d error=%d\n",
			    i, rv);
			goto out;
		}
	}

	delay(5000);
	rv = mwx_mcu_start_patch(sc);
	if (rv != 0) {
		MWX_DEV_LOG(sc, "patch start failed error=%d\n", rv);
		goto out;
	}
	MWX_DEV_LOG(sc, "patch start ok\n");

out:
	sem = mwx_mcu_patch_sem_ctrl(sc, 0);
	if (sem != 0)
		rv = sem;
	if (rv != 0)
		goto fail;

	if (fwlen < sizeof(*fwhdr)) {
		MWX_DEV_LOG(sc, "invalid WM firmware size=%zu min=%zu\n",
		    fwlen, sizeof(*fwhdr));
		rv = EINVAL;
		goto fail;
	}
	fwhdr = (struct mwx_fw_trailer *)(fwbuf + fwlen - sizeof(*fwhdr));
	MWX_DEV_LOG(sc, "WM header version=%.10s build=%.15s chip=0x%02x eco=0x%02x regions=%u\n",
	    fwhdr->fw_ver, fwhdr->build_date, fwhdr->chip_id,
	    fwhdr->eco_code, fwhdr->n_region);

	if (fwlen < sizeof(*fwhdr) +
	    fwhdr->n_region * sizeof(struct mwx_fw_region)) {
		MWX_DEV_LOG(sc, "invalid WM firmware short header size=%zu regions=%u\n",
		    fwlen, fwhdr->n_region);
		rv = EINVAL;
		goto fail;
	}

	for (i = 0; i < fwhdr->n_region; i++) {
		struct mwx_fw_region *region;
		uint32_t len, addr, mode;

		region = (struct mwx_fw_region *)((u_char *)fwhdr -
		    (fwhdr->n_region - i) * sizeof(*region));

		addr = le32toh(region->addr);
		len = le32toh(region->len);
		mode = mwx_mcu_gen_dl_mode(region->feature_set);

		if (region->feature_set & FW_FEATURE_OVERRIDE_ADDR)
			override = addr;
		MWX_DEV_LOG(sc, "WM region index=%d addr=0x%08x len=%u mode=0x%08x feature=0x%02x offset=%zu\n",
		    i, addr, len, mode, region->feature_set, offset);

		if (offset + len > fwlen - sizeof(*fwhdr) -
		    fwhdr->n_region * sizeof(struct mwx_fw_region)) {
			MWX_DEV_LOG(sc, "WM region index=%d exceeds payload len=%u offset=%zu\n",
			    i, len, offset);
			rv = EINVAL;
			goto fail;
		}

		if (region->feature_set & FW_FEATURE_NON_DL) {
			offset += len;
			continue;
		}

		rv = mwx_mcu_init_download(sc, addr, len, mode);
		if (rv != 0) {
			MWX_DEV_LOG(sc, "WM download request failed index=%d error=%d\n",
			    i, rv);
			goto fail;
		}

		rv = mwx_mcu_send_firmware(sc, MCU_CMD_FW_SCATTER,
		    fwbuf + offset, len, 4096);
		if (rv != 0) {
			MWX_DEV_LOG(sc, "WM send failed index=%d error=%d\n",
			    i, rv);
			goto fail;
		}
		offset += len;
	}

	if (override != 0)
		option |= FW_START_OVERRIDE;

	rv = mwx_mcu_start_firmware(sc, override, option);
	if (rv != 0) {
		MWX_DEV_LOG(sc, "firmware start failed override=0x%08x option=0x%08x error=%d\n",
		    override, option, rv);
		goto fail;
	}
	MWX_DEV_LOG(sc, "firmware start ok override=0x%08x option=0x%08x\n",
	    override, option);

	/* XXX should not busy poll here */
	if (mwx_poll(sc, MT_CONN_ON_MISC, MT_TOP_MISC2_FW_N9_RDY,
	    MT_TOP_MISC2_FW_N9_RDY, 1500) != 0) {
		MWX_DEV_LOG(sc, "firmware ready poll timed out\n");
		rv = EIO;
		goto fail;
	}

	MWX_DEV_LOG(sc, "firmware loaded and ready\n");
	if (gMwxHal != NULL)
		gMwxHal->mwxStartPoll(sc);
	rv = 0;

fail:
	if (buf != NULL)
		free(buf, M_DEVBUF, buflen);
	if (fwbuf != NULL)
		free(fwbuf, M_DEVBUF, fwlen);
	return rv;
}

int
mwx_mac_wtbl_update(struct mwx_softc *sc, int idx)
{
	mwx_rmw(sc, MT_WTBL_UPDATE,
	    (idx & MT_WTBL_UPDATE_WLAN_IDX_MASK) | MT_WTBL_UPDATE_ADM_COUNT_CLEAR,
	    MT_WTBL_UPDATE_WLAN_IDX_MASK);

	return mwx_poll(sc, MT_WTBL_UPDATE, 0, MT_WTBL_UPDATE_BUSY, 5000);
}

void
mwx_mac_init_band(struct mwx_softc *sc, uint32_t band)
{
	mwx_rmw(sc, MT_TMAC_CTCR0(band), 0x3f, MT_TMAC_CTCR0_INS_DDLMT_REFTIME);
	mwx_set(sc, MT_TMAC_CTCR0(band),
	    MT_TMAC_CTCR0_INS_DDLMT_VHT_SMPDU_EN |
	    MT_TMAC_CTCR0_INS_DDLMT_EN);

	mwx_set(sc, MT_WF_RMAC_MIB_TIME0(band), MT_WF_RMAC_MIB_RXTIME_EN);
	mwx_set(sc, MT_WF_RMAC_MIB_AIRTIME0(band), MT_WF_RMAC_MIB_RXTIME_EN);

	/* enable MIB tx-rx time reporting */
	mwx_set(sc, MT_MIB_SCR1(band), MT_MIB_TXDUR_EN);
	mwx_set(sc, MT_MIB_SCR1(band), MT_MIB_RXDUR_EN);

	mwx_rmw(sc, MT_DMA_DCR0(band),
	    1536 << MT_DMA_DCR0_MAX_RX_LEN_SHIFT, MT_DMA_DCR0_MAX_RX_LEN_MASK);
	/* disable rx rate report by default due to hw issues */
	mwx_clear(sc, MT_DMA_DCR0(band), MT_DMA_DCR0_RXD_G5_EN);

	/* filter out non-resp frames and get instantaneous signal reporting */
	mwx_rmw(sc, MT_WTBLOFF_TOP_RSCR(band),
	    0 << MT_WTBLOFF_TOP_RSCR_RCPI_MODE_SHIFT |
	    0x3 << MT_WTBLOFF_TOP_RSCR_RCPI_PARAM_SHIFT,
	    MT_WTBLOFF_TOP_RSCR_RCPI_MODE_MASK |
	    MT_WTBLOFF_TOP_RSCR_RCPI_PARAM_MASK);
}

void
mwx_mac_init(struct mwx_softc *sc)
{
	int i;

	mwx_rmw(sc, MT_MDP_DCR1, 1536 << MT_MDP_DCR1_MAX_RX_LEN_SHIFT,
	    MT_MDP_DCR1_MAX_RX_LEN_MASK);

	/* enable hardware de-agg */
	mwx_set(sc, MT_MDP_DCR0, MT_MDP_DCR0_DAMSDU_EN);
#if 0
	/* not enabled since our stack does not handle 802.3 frames */
	/* enable hardware rx header translation */
	mwx_set(sc, MT_MDP_DCR0, MT_MDP_DCR0_RX_HDR_TRANS_EN);
#endif

	for (i = 0; i < MWX_WTBL_SIZE; i++)
		mwx_mac_wtbl_update(sc, i);

	mwx_mac_init_band(sc, 0);
	mwx_mac_init_band(sc, 1);

	sc->sc_rxfilter = mwx_read(sc, MT_WF_RFCR(0));
}

int
mwx_mcu_patch_sem_ctrl(struct mwx_softc *sc, int semget)
{
#define	PATCH_SEM_RELEASE		0
#define PATCH_SEM_GET			1
#define	PATCH_NOT_DL_SEM_FAIL		0
#define	PATCH_IS_DL			1
#define	PATCH_NOT_DL_SEM_SUCCESS	2
#define	PATCH_REL_SEM_SUCCESS		3

	uint32_t op = semget ? PATCH_SEM_GET : PATCH_SEM_RELEASE;
	struct {
		uint32_t op;
	} req = {
		.op = htole32(op),
	};
	int rv, seq;
	uint32_t sem;

	rv = mwx_mcu_send_msg(sc, MCU_CMD_PATCH_SEM_CONTROL,
	    &req, sizeof(req), &seq);
	if (rv != 0)
		return rv;

	rv = mwx_mcu_wait_resp_int(sc, MCU_CMD_PATCH_SEM_CONTROL, seq, &sem);
	if (rv != 0)
		return rv;

	if (semget) {
		switch (sem) {
		case PATCH_IS_DL:
			return -1;
		case PATCH_NOT_DL_SEM_SUCCESS:
			return 0;
		default:
			DPRINTF("%s: failed to %s patch semaphore\n",
			    DEVNAME(sc), "get");
			return EAGAIN;
		}
	} else {
		switch (sem) {
		case PATCH_REL_SEM_SUCCESS:
			return 0;
		default:
			DPRINTF("%s: failed to %s patch semaphore\n",
			    DEVNAME(sc), "release");
			return EAGAIN;
		}
	}
}

int
mwx_mcu_init_download(struct mwx_softc *sc, uint32_t addr,
    uint32_t len, uint32_t mode)
{
	struct {
		uint32_t addr;
		uint32_t len;
		uint32_t mode;
	} req = {
		.addr = htole32(addr),
		.len = htole32(len),
		.mode = htole32(mode),
	};
	int cmd;

	if (addr == 0x200000 || addr == 0x900000)
		cmd = MCU_CMD_PATCH_START_REQ;
	else
		cmd = MCU_CMD_TARGET_ADDRESS_LEN_REQ;

	return mwx_mcu_send_wait(sc, cmd, &req, sizeof(req));
}

int
mwx_mcu_send_firmware(struct mwx_softc *sc, int cmd, u_char *data,
    size_t len, size_t max_len)
{
	size_t cur_len;
	int rv;

	/* mt76: max_len = 4096 - headroom; macOS'ta 4096+baslik tek 4KB kume + tek DMA segmentine sigmaz */
	if (max_len > 4096 - sizeof(struct mwx_mcu_txd))
		max_len = 4096 - sizeof(struct mwx_mcu_txd);

	while (len > 0) {
		cur_len = len;
		if (cur_len > max_len)
			cur_len = max_len;

		rv = mwx_mcu_send_msg(sc, cmd, data, cur_len, NULL);
		if (rv != 0) {
			MWX_DEV_LOG(sc, "send_firmware chunk FAIL cur_len=%zu remain=%zu max_len=%zu rv=%d\n",
			    cur_len, len, max_len, rv);
			return rv;
		}

		data += cur_len;
		len -= cur_len;

		mwx_dma_tx_cleanup(sc, &sc->sc_txfwdlq);
	}

	return 0;
}

int
mwx_mcu_start_patch(struct mwx_softc *sc)
{
	struct {
		uint8_t check_crc;
		uint8_t reserved[3];
	} req = {
		.check_crc = 0,
	};

	return mwx_mcu_send_wait(sc, MCU_CMD_PATCH_FINISH_REQ, &req,
	    sizeof(req));
}

int
mwx_mcu_start_firmware(struct mwx_softc *sc, uint32_t addr, uint32_t option)
{
	struct {
		uint32_t option;
		uint32_t addr;
	} req = {
		.option = htole32(option),
		.addr = htole32(addr),
	};

	return mwx_mcu_send_wait(sc, MCU_CMD_FW_START_REQ, &req, sizeof(req));
}

int
mt7921_mcu_get_nic_capability(struct mwx_softc *sc)
{
	struct mt76_connac_cap_hdr {
		uint16_t	n_elements;
		uint16_t	pad;
	} __packed *hdr;
	struct tlv_hdr {
		uint32_t	type;
		uint32_t	len;
	} __packed *tlv;
	struct mt76_connac_phy_cap {
		uint8_t		ht;
		uint8_t		vht;
		uint8_t		_5g;
		uint8_t		max_bw;
		uint8_t		nss;
		uint8_t		dbdc;
		uint8_t		tx_ldpc;
		uint8_t		rx_ldpc;
		uint8_t		tx_stbc;
		uint8_t		rx_stbc;
		uint8_t		hw_path;
		uint8_t		he;
	} __packed *cap;
	struct mbuf *m;
	int rv, seq, count, i;

	rv = mwx_mcu_send_msg(sc, MCU_CE_CMD_GET_NIC_CAPAB, NULL, 0, &seq);
	if (rv != 0)
		return rv;

	rv = mwx_mcu_wait_resp_msg(sc, MCU_CE_CMD_GET_NIC_CAPAB, seq, &m);
	if (rv != 0)
		return rv;

	/* the message was already pulled up by mwx_mcu_rx_event() */
	if (mbuf_len(m) < sizeof(*hdr)) {
		smtw_trace("%s: GET_NIC_CAPAB response size error\n", DEVNAME(sc));
		m_freem(m);
		return EINVAL;
	}
	hdr = mtod(m, struct mt76_connac_cap_hdr *);
	count = le16toh(hdr->n_elements);
	m_adj(m, sizeof(*hdr));

	for (i = 0; i < count; i++) {
		uint32_t type, len;

		if (mbuf_len(m) < sizeof(*tlv)) {
			smtw_trace("%s: GET_NIC_CAPAB tlv size error\n",
			    DEVNAME(sc));
			m_freem(m);
			return EINVAL;
		}

		tlv = mtod(m, struct tlv_hdr *);
		type = le32toh(tlv->type);
		len = le32toh(tlv->len);
		m_adj(m, sizeof(*tlv));

		if (mbuf_len(m) < len)
			break;
		switch (type) {
		case MT_NIC_CAP_6G:
			/* TODO 6GHZ SUPPORT */
			sc->sc_capa.has_6ghz = /* XXX 1 */ 0;
			break;
		case MT_NIC_CAP_MAC_ADDR:
			if (len < ETHER_ADDR_LEN)
				break;
			memcpy(sc->sc_lladdr, mtod(m, caddr_t), ETHER_ADDR_LEN);
			break;
		case MT_NIC_CAP_PHY:
			if (len < sizeof(*cap))
				break;
			cap = mtod(m, struct mt76_connac_phy_cap *);

			sc->sc_capa.num_streams = cap->nss;
			sc->sc_capa.antenna_mask = (1U << cap->nss) - 1;
			sc->sc_capa.has_2ghz = cap->hw_path & 0x01;
			sc->sc_capa.has_5ghz = cap->hw_path & 0x02;
			break;
		case MT_NIC_CAP_TX_RESOURCE:
			/* unused on PCIe devices */
			break;
		}
		m_adj(m, len);
	}

	smtw_trace("%s: address %s\n", DEVNAME(sc), ether_sprintf(sc->sc_lladdr));

	m_freem(m);
	return 0;
}

int
mt7921_mcu_fw_log_2_host(struct mwx_softc *sc, uint8_t ctrl)
{
	struct {
		uint8_t	ctrl;
		uint8_t	pad[3];
	} req = {
		.ctrl = ctrl,
	};

	return mwx_mcu_send_msg(sc, MCU_CE_CMD_FWLOG_2_HOST, &req,
	    sizeof(req), NULL);
}

int
mt7921_mcu_set_eeprom(struct mwx_softc *sc)
{
	struct req_hdr {
		uint8_t	buffer_mode;
		uint8_t	format;
		uint8_t pad[2];
	} req = {
		.buffer_mode = EE_MODE_EFUSE,
		.format = EE_FORMAT_WHOLE,
	};

	return mwx_mcu_send_wait(sc, MCU_EXT_CMD_EFUSE_BUFFER_MODE, &req,
	    sizeof(req));
}

int
mt7921_mcu_set_rts_thresh(struct mwx_softc *sc, uint32_t val, uint8_t band)
{
	struct {
		uint8_t		prot_idx;
		uint8_t		band;
		uint8_t		rsv[2];
		uint32_t	len_thresh;
		uint32_t	pkt_thresh;
	} __packed req = {
		.prot_idx = 1,
		.band = band,
		.len_thresh = htole32(val),
		.pkt_thresh = htole32(0x2),
	};

	return mwx_mcu_send_wait(sc, MCU_EXT_CMD_PROTECT_CTRL, &req,
	    sizeof(req));
}

int
mwx_mcu_set_deep_sleep(struct mwx_softc *sc, int ena)
{
	struct mwx_connac_config req = {
		.resp_type = 0,
	};

	DPRINTF("%s: %s deep sleep\n", DEVNAME(sc), ena ? "enable" : "disable");
	/*
	 * R9T: AMD PCIe uzerinde derin uykuyu hic acma. ASSOC'ta "KeepFullPwr 0" gonderildiginde assoc istegi
	 * TX tamamlanmiyor / AP'ye gitmiyor (trace8 bulgusu). Her zaman tam guc.
	 */
	if (ena)
		MWX_DEV_LOG(sc, "deep sleep istegi yok sayildi (KeepFullPwr 1 zorlandi)\n");
	ena = 0;
	snprintf((char *)req.data, sizeof(req.data), "KeepFullPwr %d", !ena);
	return mwx_mcu_send_msg(sc, MCU_CE_CMD_CHIP_CONFIG, &req,
	     sizeof(req), NULL);
}

void
mt7921_mcu_low_power_event(struct mwx_softc *sc, struct mbuf *m)
{
	struct mt7921_mcu_lp_event {
		uint8_t state;
		uint8_t reserved[3];
	} __packed *event;

	if (mbuf_len(m) < sizeof(*event))
		return;
	event = mtod(m, struct mt7921_mcu_lp_event *);
	DPRINTF("%s: low power event state %d\n", DEVNAME(sc), event->state);
}

void
mt7921_mcu_tx_done_event(struct mwx_softc *sc, struct mbuf *m)
{
	struct mt7921_mcu_tx_done_event {
		uint8_t		pid;
		uint8_t		status;
		uint16_t	seq;
		uint8_t		wlan_idx;
		uint8_t		tx_cnt;
		uint16_t	tx_rate;
		uint8_t		flag;
		uint8_t		tid;
		uint8_t		rsp_rate;
		uint8_t		mcs;
		uint8_t		bw;
		uint8_t		tx_pwr;
		uint8_t		reason;
		uint8_t		rsv0[1];
		uint32_t	delay;
		uint32_t	timestamp;
		uint32_t	applied_flag;
		uint8_t		txs[28];
		uint8_t		rsv1[32];
	} __packed *event;

	if (mbuf_len(m) < sizeof(*event))
		return;
	event = mtod(m, struct mt7921_mcu_tx_done_event *);
	// TODO mt7921_mac_add_txs(dev, event->txs);
}

int
mt7921_mcu_hw_scan(struct mwx_softc *sc, int bgscan)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_channel *c;
	struct mt76_connac_hw_scan_req	*req;
	struct mbuf *m;
	int n_ssids = 0;
	int rv;
	uint8_t nchan, n_2g = 0, n_5g = 0;

	m = mwx_mcu_alloc_msg(sizeof(*req));
	if (m == NULL)
		return ENOMEM;
	req = mtod(m, struct mt76_connac_hw_scan_req *);
	/* The mbuf payload is not initialized by allocation. */
	memset(req, 0, sizeof(*req));

	sc->sc_flags |= MWX_FLAG_SCANNING;
	sc->sc_scan_seq_num = (sc->sc_scan_seq_num + 1) & 0x7f;

	req->seq_num = sc->sc_scan_seq_num /* | sc->sc_band_idx << 7 */;
	req->bss_idx = /* mvif->idx */ 0;
	req->scan_type = /* sreq->n_ssids ? 1 : */ 0;
	req->probe_req_num = /* sreq->n_ssids ? 2 : */ 0;
	req->version = 1;

#ifdef NOTYET
	for (i = 0; i < sreq->n_ssids; i++) {
		if (!sreq->ssids[i].ssid_len)
			continue;
		req->ssids[i].ssid_len = htole32(sreq->ssids[i].ssid_len);
		memcpy(req->ssids[i].ssid, sreq->ssids[i].ssid,
		    sreq->ssids[i].ssid_len);
		n_ssids++;
	}
#endif

	req->ssid_type = n_ssids ? 0x4 : 0x1;
	req->ssid_type_ext = n_ssids ? 1 : 0;
	req->ssids_num = n_ssids;

	for (nchan = 0, c = &ic->ic_channels[1];
	    c <= &ic->ic_channels[IEEE80211_CHAN_MAX] &&
	    nchan < 64; c++) {
		struct mt76_connac_mcu_scan_channel *chan;
		uint8_t channel_num;

		if (c->ic_flags == 0)
			continue;

		if (nchan < 32)
			chan = &req->channels[nchan];
		else
			chan = &req->ext_channels[nchan - 32];

		channel_num = ieee80211_mhz2ieee(c->ic_freq, 0);
		/* TODO IEEE80211_IS_CHAN_6GHZ -> chan->band = 3*/
		if (IEEE80211_IS_CHAN_2GHZ(c)) {
			chan->band = 1;
			n_2g++;
		} else {
			chan->band = 2;
			n_5g++;
		}
		chan->channel_num = channel_num;
		nchan++;
	}

	if (nchan <= 32) {
		req->channels_num = nchan;
	} else {
		req->channels_num = 32;
		req->ext_channels_num = nchan - 32;
	}

	req->channel_type = nchan ? 4 : 0;
	req->timeout_value = htole16(nchan * 120);
	req->channel_min_dwell_time = htole16(120);
	req->channel_dwell_time = htole16(120);


#ifdef NOTYET
	if (sreq->ie_len > 0) {
		memcpy(req->ies, sreq->ie, sreq->ie_len);
		req->ies_len = htole16(sreq->ie_len);
	}
#endif

	req->scan_func |= SCAN_FUNC_SPLIT_SCAN;

	/* wildcard BSSID */
	memset(req->bssid, 0xff, ETHER_ADDR_LEN);
#ifdef NOTYET
	if (sreq->flags & NL80211_SCAN_FLAG_RANDOM_ADDR) {
		get_random_mask_addr(req->random_mac, sreq->mac_addr,
		    sreq->mac_addr_mask);
		req->scan_func |= SCAN_FUNC_RANDOM_MAC;
	}
#endif

	MWX_DEV_LOG(sc,
	    "scan request seq=%u channels=%u 2g=%u 5g=%u primary=%u ext=%u timeout=%u dwell=%u min_dwell=%u scan_func=0x%08x ssid_type=0x%x\n",
	    sc->sc_scan_seq_num, nchan, n_2g, n_5g, req->channels_num,
	    req->ext_channels_num, le16toh(req->timeout_value),
	    le16toh(req->channel_dwell_time),
	    le16toh(req->channel_min_dwell_time),
	    le32toh(req->scan_func), req->ssid_type);

	rv = mwx_mcu_send_mbuf(sc, MCU_CE_CMD_START_HW_SCAN, m, NULL);
	if (rv != 0)
		sc->sc_flags &= ~(MWX_FLAG_SCANNING | MWX_FLAG_BGSCAN);

	return rv;
}

int
mt7921_mcu_hw_scan_cancel(struct mwx_softc *sc)
{
	struct {
		uint8_t		seq_num;
		uint8_t		is_ext_channel;
		uint8_t		rsv[2];
	} __packed req = {
		.seq_num = sc->sc_scan_seq_num,
	};
	int rv;

	rv = mwx_mcu_send_msg(sc, MCU_CE_CMD_CANCEL_HW_SCAN, &req,
	    sizeof(req), NULL);
	if (rv == 0)
		sc->sc_flags &= ~(MWX_FLAG_SCANNING | MWX_FLAG_BGSCAN);
	return rv;
}

void
mwx_end_scan_task(void *arg)
{
	struct mwx_softc *sc = (struct mwx_softc *)arg;
	struct ieee80211com *ic = &sc->sc_ic;
	int s;

	s = splnet();
	ieee80211_end_scan(&ic->ic_if);
	splx(s);
}

void
mt7921_mcu_scan_event(struct mwx_softc *sc, struct mbuf *m)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct mt76_connac_hw_scan_done *event;
	uint32_t probe_sum = 0, mdrdy_sum = 0;
	int i, nodes_before;

	sc->sc_scan_done_count++;
	nodes_before = mwx_count_nodes(ic);
	sc->sc_scan_last_ieee80211_nodes = nodes_before;

	if (mbuf_len(m) >= sizeof(*event)) {
		event = mtod(m, struct mt76_connac_hw_scan_done *);
		for (i = 0; i < MT76_HW_SCAN_DONE_MAX_CHANNEL_NUM; i++) {
			probe_sum += event->beacon_probe_num[i];
			mdrdy_sum += event->mdrdy_count[i];
		}
		MWX_DEV_LOG(sc,
		    "scan done #%u seq=%u complete_ch=%u state=%u version=%u beacon_scan=%u probe_sum=%u mdrdy_sum=%u beacon_2g=%u beacon_5g=%u nodes_before=%d rx_norm=%u rx_fail=%u\n",
		    sc->sc_scan_done_count, event->seq_num,
		    event->complete_channel_num, event->current_state,
		    event->version, le32toh(event->beacon_scan_num),
		    probe_sum, mdrdy_sum, le32toh(event->beacon_2g_num),
		    le32toh(event->beacon_5g_num), nodes_before,
		    sc->sc_rx_pkt_normal, sc->sc_rx_fill_fail);
	} else {
		MWX_DEV_LOG(sc,
		    "scan done #%u short event len=%zu nodes_before=%d rx_norm=%u rx_fail=%u\n",
		    sc->sc_scan_done_count, mbuf_len(m), nodes_before,
		    sc->sc_rx_pkt_normal, sc->sc_rx_fill_fail);
	}

	if (mt7921_mcu_hw_scan_cancel(sc) != 0)
		return;
	task_add(systq, &sc->sc_scan_task);
}

int
mt7921_mcu_set_mac_enable(struct mwx_softc *sc, int band, int enable)
{
	struct {
		uint8_t		enable;
		uint8_t		band;
		uint8_t		rsv[2];
	} __packed req = {
		.enable = (uint8_t)enable,
		.band = (uint8_t)band,
	};

	return mwx_mcu_send_wait(sc, MCU_EXT_CMD_MAC_INIT_CTRL, &req,
	    sizeof(req));
}

int
mt7921_mcu_set_channel_domain(struct mwx_softc *sc)
{
	struct {
		uint8_t		alpha2[4]; /* regulatory_request.alpha2 */
		uint8_t		bw_2g;	/* BW_20_40M		0
					 * BW_20M		1
					 * BW_20_40_80M		2
					 * BW_20_40_80_160M	3
					 * BW_20_40_80_8080M	4
					 */
		uint8_t		bw_5g;
		uint8_t		bw_6g;
		uint8_t		pad;
		uint8_t		n_2ch;
		uint8_t		n_5ch;
		uint8_t		n_6ch;
		uint8_t		pad2;
	} __packed *hdr;
	struct {
		uint16_t	hw_value;
		uint16_t	pad;
		uint32_t	flags;
	} __packed *channel;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_channel *chan;
	struct mbuf *m;
	int i, len, rv;
	int n_2ch = 0, n_5ch = 0, n_6ch = 0;

	len = sizeof(*hdr) + IEEE80211_CHAN_MAX * sizeof(channel);
	m = mwx_mcu_alloc_msg(len);
	if (m == NULL)
		return ENOMEM;
	hdr = (decltype(hdr))mbuf_data(m);

	hdr->alpha2[0] = '0';
	hdr->alpha2[1] = '0';

	channel = (decltype(channel))(hdr + 1);

	hdr->bw_2g = 0;	/* BW_20_40M */
	for (i = 0; i <= IEEE80211_CHAN_MAX; i++) {
		chan = &ic->ic_channels[i];
		if (!IEEE80211_IS_CHAN_2GHZ(chan))
			continue;

		channel->hw_value = htole16(ieee80211_chan2ieee(ic, chan));
		channel->flags = htole32(0);	/* XXX */

		channel++;
		n_2ch++;
	}
	hdr->bw_5g = 3; /* BW_20_40_80_160M */
	for (i = 0; i <= IEEE80211_CHAN_MAX; i++) {
		chan = &ic->ic_channels[i];
		if (!IEEE80211_IS_CHAN_5GHZ(chan))
			continue;

		channel->hw_value = htole16(ieee80211_chan2ieee(ic, chan));
		channel->flags = htole32(0);	/* XXX */

		channel++;
		n_5ch++;
	}
#ifdef NOTYET
	/* 6GHz handling */
	hdr->bw_6g = 3; /* BW_20_40_80_160M */
	for (i = 0; i <= IEEE80211_CHAN_MAX; i++) {
		chan = &ic->ic_channels[i];
		if (!IEEE80211_IS_CHAN_6GHZ(chan))
			continue;

		channel->hw_value = htole16(ieee80211_chan2ieee(ic, chan));
		channel->flags = htole32(0);	/* XXX */

		channel++;
		n_6ch++;
	}
#endif

	memcpy(hdr->alpha2, sc->sc_alpha2, sizeof(sc->sc_alpha2));
	hdr->n_2ch = n_2ch;
	hdr->n_5ch = n_5ch;
	hdr->n_6ch = n_6ch;

	mwx_mcu_set_len(m, channel);
	rv = mwx_mcu_send_mbuf(sc, MCU_CE_CMD_SET_CHAN_DOMAIN, m, NULL);
	return rv;
}

/*
 * R9T trace33: iliskili BSS icin anlasilan genislik (ni_chw) ve merkez kanal.
 * Kanal bayraklari yetenek bildirir; gercek genislik net80211'in
 * ht/vht_negotiate'inde ni_chw + ic_center_freq1 olarak belirlenir.
 */
static uint8_t
mwx_bss_bw(struct mwx_softc *sc, uint8_t *center)
{
	struct ieee80211_node *ni = sc->sc_ic.ic_bss;
	struct ieee80211_channel *c;
	uint8_t bw = CMD_CBW_20MHZ;

	if (ni == NULL || ni->ni_chan == NULL || ni->ni_chan == IEEE80211_CHAN_ANYC)
		return CMD_CBW_20MHZ;
	c = ni->ni_chan;
	*center = ieee80211_mhz2ieee(c->ic_freq, c->ic_flags);
	if (!(ni->ni_flags & (IEEE80211_NODE_HT | IEEE80211_NODE_VHT)))
		return CMD_CBW_20MHZ;
	switch (ni->ni_chw) {
	case IEEE80211_CHAN_WIDTH_40:
		bw = CMD_CBW_40MHZ;
		break;
	case IEEE80211_CHAN_WIDTH_80:
		bw = CMD_CBW_80MHZ;
		break;
	case IEEE80211_CHAN_WIDTH_160:
		bw = CMD_CBW_160MHZ;
		break;
	default:
		return CMD_CBW_20MHZ;
	}
	if (c->ic_center_freq1 != 0)
		*center = ieee80211_mhz2ieee(c->ic_center_freq1, c->ic_flags);
	return bw;
}

/* YYavic radio bridge (ABI 5): operating width + last RX rate. */
extern "C" void
mwx_yy_link_info(struct ieee80211com *ic, uint8_t *bss_bw, uint8_t *center, uint8_t *rx)
{
	struct mwx_softc *sc = gMwxPollSc;
	uint8_t c = 0;
	*bss_bw = (sc != NULL && &sc->sc_ic == ic) ? mwx_bss_bw(sc, &c) : 0;
	*center = c;
	rx[0] = gYYRxMode; rx[1] = gYYRxMcs; rx[2] = gYYRxNss; rx[3] = gYYRxBw; rx[4] = gYYRxSgi;
}

uint8_t
mt7921_mcu_chan_bw(struct ieee80211_channel *channel)
{
	/*
	 * following modes are not yet supported
	 *  CMD_CBW_5MHZ, CMD_CBW_10MHZ, CMD_CBW_8080MHZ
	 */
	if (channel->ic_flags & IEEE80211_CHAN_VHT160)
		return CMD_CBW_160MHZ;
	if (channel->ic_flags & IEEE80211_CHAN_VHT80)
		return CMD_CBW_80MHZ;
	if (channel->ic_flags & IEEE80211_CHAN_40MHZ)
		return CMD_CBW_40MHZ;
	return CMD_CBW_20MHZ;
}

int
mt7921_mcu_set_chan_info(struct mwx_softc *sc, int cmd)
{
	struct ieee80211_channel *channel;
	struct {
		uint8_t		control_ch;
		uint8_t		center_ch;
		uint8_t		bw;
		uint8_t		tx_streams_num;
		uint8_t		rx_streams;	/* mask or num */
		uint8_t		switch_reason;
		uint8_t		band_idx;
		uint8_t		center_ch2;	/* for 80+80 only */
		uint16_t	cac_case;
		uint8_t		channel_band;
		uint8_t		rsv0;
		uint32_t	outband_freq;
		uint8_t		txpower_drop;
		uint8_t		ap_bw;
		uint8_t		ap_center_ch;
		uint8_t		rsv1[57];
	} __packed req = {
		.tx_streams_num = sc->sc_capa.num_streams,
		.rx_streams = sc->sc_capa.antenna_mask,
		.band_idx = 0,	/* XXX 0 or 1 */
	};

	if (sc->sc_ic.ic_opmode == IEEE80211_M_STA && sc->sc_ic.ic_bss != NULL)
		channel = sc->sc_ic.ic_bss->ni_chan;
	else
		channel = sc->sc_ic.ic_ibss_chan;

	req.control_ch = ieee80211_mhz2ieee(channel->ic_freq,
		    channel->ic_flags);
	req.center_ch = ieee80211_mhz2ieee(channel->ic_freq,
		    channel->ic_flags);
	req.bw = CMD_CBW_20MHZ;
	if (sc->sc_ic.ic_opmode == IEEE80211_M_STA && sc->sc_ic.ic_bss != NULL &&
	    channel == sc->sc_ic.ic_bss->ni_chan)
		req.bw = mwx_bss_bw(sc, &req.center_ch);	/* R9T trace33 */
	{
		static uint32_t n;
		if (n++ < 16)
			MWX_DEV_LOG(sc, "chan_info cmd=0x%x ctrl=%u center=%u bw=%u\n",
			    cmd, req.control_ch, req.center_ch, req.bw);
	}

#ifdef NOTYET
	if (channel->ic_flags & IEEE80211_CHAN_6GHZ)
		req.channel_band = 2;
	else
#endif
	if (channel->ic_flags & IEEE80211_CHAN_5GHZ)
		req.channel_band = 1;
	else
		req.channel_band = 0;

#ifdef NOTYET
	if (dev->mt76.hw->conf.flags & IEEE80211_CONF_OFFCHANNEL)
		req.switch_reason = CH_SWITCH_SCAN_BYPASS_DPD;
	else if ((chandef->chan->flags & IEEE80211_CHAN_RADAR) &&
		 chandef->chan->dfs_state != NL80211_DFS_AVAILABLE)
		req.switch_reason = CH_SWITCH_DFS;
	else
#endif
		req.switch_reason = CH_SWITCH_NORMAL;

	if (cmd == MCU_EXT_CMD_CHANNEL_SWITCH)
		req.rx_streams = sc->sc_capa.num_streams;

#ifdef NOTYET
	if (chandef->width == NL80211_CHAN_WIDTH_80P80) {
		int freq2 = chandef->center_freq2;
		req.center_ch2 = ieee80211_frequency_to_channel(freq2);
	}
#endif

	return mwx_mcu_send_wait(sc, cmd, &req, sizeof(req));
}

/* hardcoded version of what linux does */
void
mt7921_mcu_build_sku(struct mwx_softc *sc, int band, int8_t *sku)
{
	int max_power = 127;
	int i, offset = 4;

	memset(sku, max_power, MT_SKU_POWER_LIMIT);

	if (band == MT_TX_PWR_BAND_2GHZ) {
		/* cck */
		memset(sku, 0x28, 4);
	}

	/* ofdm */
	memset(sku + offset, 0x28, 8);
	offset += 8;

	/* ht */
	for (i = 0; i < 2; i++) {
		memset(sku + offset, 0x28, 8);
		offset += 8;
	}
	sku[offset++] = 0x28;

	/* vht */
	for (i = 0; i < 4; i++) {
		/* this only sets 10 out of 12 bytes on purpose */
		memset(sku + offset, 0x28, 10);
		offset += 12;
	}

	/* he */
	for (i = 0; i < 7; i++) {
		memset(sku + offset, 0x28, 12);
		offset += 12;
	}
}

int
mt7921_mcu_rate_txpower_band(struct mwx_softc *sc, int band,
    const uint8_t *chans, int n_chans, int is_last)
{
	struct mt76_connac_sku_tlv *sku_tlv;
	struct mt76_connac_tx_power_limit_tlv *tx_power_tlv;
	struct mbuf *m;
	int batch_size = 8;
	const int len = sizeof(*tx_power_tlv) + batch_size * sizeof(*sku_tlv);
	int rv = 0, idx, j;

	for (idx = 0; idx < n_chans; ) {
		int num_ch = batch_size;

		m = mwx_mcu_alloc_msg(len);
		if (m == NULL)
			return ENOMEM;
		tx_power_tlv = mtod(m, struct mt76_connac_tx_power_limit_tlv *);
		tx_power_tlv->n_chan = num_ch;
		tx_power_tlv->band = band;
		memcpy(tx_power_tlv->alpha2, sc->sc_alpha2,
		    sizeof(sc->sc_alpha2));

		if (n_chans - idx < batch_size) {
			num_ch = n_chans - idx;
			if (is_last)
				tx_power_tlv->last_msg = 1;
		}

		sku_tlv = (struct mt76_connac_sku_tlv *)(tx_power_tlv + 1);

		for (j = 0; j < num_ch; j++, idx++) {
			sku_tlv->channel = chans[idx];
			mt7921_mcu_build_sku(sc, band, sku_tlv->pwr_limit);
			sku_tlv++;
		}

		mwx_mcu_set_len(m, sku_tlv);
		rv = mwx_mcu_send_mbuf(sc, MCU_CE_CMD_SET_RATE_TX_POWER, m,
		    NULL);
		if (rv != 0)
			break;
	}

	return rv;
}

int
mt7921_mcu_set_rate_txpower(struct mwx_softc *sc)
{
	static const uint8_t chan_list_2ghz[] = {
	    1, 2,  3,  4,  5,  6,  7,
	    8, 9, 10, 11, 12, 13, 14
	};
	static const uint8_t chan_list_5ghz[] = {
	    36,  38,  40,  42,  44,  46,  48,
	    50,  52,  54,  56,  58,  60,  62,
	    64, 100, 102, 104, 106, 108, 110,
	    112, 114, 116, 118, 120, 122, 124,
	    126, 128, 132, 134, 136, 138, 140,
	    142, 144, 149, 151, 153, 155, 157,
	    159, 161, 165
	};
	static const uint8_t chan_list_6ghz[] = {
	    1,   3,   5,   7,   9,  11,  13,
	    15,  17,  19,  21,  23,  25,  27,
	    29,  33,  35,  37,  39,  41,  43,
	    45,  47,  49,  51,  53,  55,  57,
	    59,  61,  65,  67,  69,  71,  73,
	    75,  77,  79,  81,  83,  85,  87,
	    89,  91,  93,  97,  99, 101, 103,
	    105, 107, 109, 111, 113, 115, 117,
	    119, 121, 123, 125, 129, 131, 133,
	    135, 137, 139, 141, 143, 145, 147,
	    149, 151, 153, 155, 157, 161, 163,
	    165, 167, 169, 171, 173, 175, 177,
	    179, 181, 183, 185, 187, 189, 193,
	    195, 197, 199, 201, 203, 205, 207,
	    209, 211, 213, 215, 217, 219, 221,
	    225, 227, 229, 233
	};
	int rv = 0;

	if (sc->sc_capa.has_2ghz)
		rv = mt7921_mcu_rate_txpower_band(sc, MT_TX_PWR_BAND_2GHZ,
		    chan_list_2ghz, nitems(chan_list_2ghz),
		    !(sc->sc_capa.has_5ghz || sc->sc_capa.has_6ghz));
	if (rv == 0 && sc->sc_capa.has_5ghz)
		rv = mt7921_mcu_rate_txpower_band(sc, MT_TX_PWR_BAND_5GHZ,
		    chan_list_5ghz, nitems(chan_list_5ghz),
		    !sc->sc_capa.has_6ghz);
	if (rv == 0 && sc->sc_capa.has_6ghz)
		rv = mt7921_mcu_rate_txpower_band(sc, MT_TX_PWR_BAND_6GHZ,
		    chan_list_6ghz, nitems(chan_list_6ghz), 1);
	return rv;
}

void
mt7921_mac_reset_counters(struct mwx_softc *sc)
{
	int i;

	for (i = 0; i < 4; i++) {
		mwx_read(sc, MT_TX_AGG_CNT(0, i));
		mwx_read(sc, MT_TX_AGG_CNT2(0, i));
	}

	/* XXX TODO stats in softc */

	/* reset airtime counters */
	mwx_read(sc, MT_MIB_SDR9(0));
	mwx_read(sc, MT_MIB_SDR36(0));
	mwx_read(sc, MT_MIB_SDR37(0));
	mwx_set(sc, MT_WF_RMAC_MIB_TIME0(0), MT_WF_RMAC_MIB_RXTIME_CLR);
	mwx_set(sc, MT_WF_RMAC_MIB_AIRTIME0(0), MT_WF_RMAC_MIB_RXTIME_CLR);
}

void
mt7921_mac_set_timing(struct mwx_softc *sc)
{
	uint16_t coverage_class = 0;		/* XXX */
	uint32_t val, reg_offset;
	uint32_t cck = MT_TIMEOUT_CCK_DEF_VAL;
	uint32_t ofdm = MT_TIMEOUT_OFDM_DEF_VAL;
	uint32_t offset;
	int is_2ghz;
	struct ieee80211_channel *tc;

	/* R9T trace29: SIFS kanala gore (2.4 GHz 10 us, 5 GHz 16 us) - Linux ile ayni */
	if (sc->sc_ic.ic_opmode == IEEE80211_M_STA && sc->sc_ic.ic_bss != NULL &&
	    sc->sc_ic.ic_bss->ni_chan != NULL && sc->sc_ic.ic_bss->ni_chan != IEEE80211_CHAN_ANYC)
		tc = sc->sc_ic.ic_bss->ni_chan;
	else
		tc = sc->sc_ic.ic_ibss_chan;
	is_2ghz = (tc == NULL || IEEE80211_IS_CHAN_2GHZ(tc));
	uint32_t sifs = is_2ghz ? 10 : 16;
	uint32_t slottime = IEEE80211_DUR_DS_SHSLOT;	/* XXX get from stack */

#ifdef NOTYET
	if (!test_bit(MT76_STATE_RUNNING, &phy->mt76->state))
		return;
#endif

	mwx_set(sc, MT_ARB_SCR(0),
	    MT_ARB_SCR_TX_DISABLE | MT_ARB_SCR_RX_DISABLE);
	delay(1);

	offset = 3 * coverage_class;
	reg_offset = offset | (offset << 16);

	mwx_write(sc, MT_TMAC_CDTR(0), cck + reg_offset);
	mwx_write(sc, MT_TMAC_ODTR(0), ofdm + reg_offset);
	mwx_write(sc, MT_TMAC_ICR0(0),
	    MT_IFS_EIFS_DEF | MT_IFS_RIFS_DEF |
	    (sifs << MT_IFS_SIFS_SHIFT) |
	    (slottime << MT_IFS_SLOT_SHIFT));

	if (slottime < 20 || !is_2ghz)
		val = MT7921_CFEND_RATE_DEFAULT;
	else
		val = MT7921_CFEND_RATE_11B;

	mwx_rmw(sc, MT_AGG_ACR0(0), val, MT_AGG_ACR_CFEND_RATE_MASK);
	{
		static uint32_t n;
		if (n++ < 12)
			MWX_DEV_LOG(sc, "set_timing is_2ghz=%d sifs=%u slot=%u icr0=0x%08x\n",
			    is_2ghz, sifs, slottime, mwx_read(sc, MT_TMAC_ICR0(0)));
	}
	mwx_clear(sc, MT_ARB_SCR(0),
	    MT_ARB_SCR_TX_DISABLE | MT_ARB_SCR_RX_DISABLE);
}

int
mt7921_mcu_uni_add_dev(struct mwx_softc *sc, struct mwx_vif *mvif,
    struct mwx_node *mn, int enable)
{
	struct {
		struct {
			uint8_t		omac_idx;
			uint8_t		band_idx;
			uint16_t	pad;
		} __packed hdr;
		struct req_tlv {
			uint16_t	tag;
			uint16_t	len;
			uint8_t		active;
			uint8_t		pad;
			uint8_t		omac_addr[ETHER_ADDR_LEN];
		} __packed tlv;
	} dev_req = {
		.hdr = {
			.omac_idx = mvif->omac_idx,
			.band_idx = mvif->band_idx,
		},
		.tlv = {
			.tag = htole16(DEV_INFO_ACTIVE),
			.len = htole16(sizeof(dev_req.tlv)),
			.active = (uint8_t)enable,
		},
	};
	struct {
		struct {
			uint8_t		bss_idx;
			uint8_t		pad[3];
		} __packed hdr;
		struct mt76_connac_bss_basic_tlv basic;
	} basic_req = {
		.hdr = {
			.bss_idx = mvif->idx,
		},
		.basic = {
			.tag = htole16(UNI_BSS_INFO_BASIC),
			.len = htole16(
			    sizeof(struct mt76_connac_bss_basic_tlv)),
			.omac_idx = mvif->omac_idx,
			.band_idx = mvif->band_idx,
			.wmm_idx = mvif->wmm_idx,
			.active = (uint8_t)enable,
			.bmc_tx_wlan_idx = htole16(mn->wcid),
			.sta_idx = htole16(mn->wcid),
			.conn_state = 1,
		},
	};
	int rv, idx, cmd, len;
	void *data;

	switch (sc->sc_ic.ic_opmode) {
	case IEEE80211_M_MONITOR:
#ifndef IEEE80211_STA_ONLY
	case IEEE80211_M_HOSTAP:
#endif
		basic_req.basic.conn_type =
		    htole32(STA_TYPE_AP | NETWORK_INFRA);
		break;
	case IEEE80211_M_STA:
		basic_req.basic.conn_type =
		    htole32(STA_TYPE_STA | NETWORK_INFRA);
		break;
#ifndef IEEE80211_STA_ONLY
	case IEEE80211_M_IBSS:
		basic_req.basic.conn_type =
		    htole32(STA_TYPE_ADHOC | NETWORK_IBSS);
		break;
#endif
	default:
		panic("%s: unknown operation mode", DEVNAME(sc));
	}

	idx = mvif->omac_idx > EXT_BSSID_START ? HW_BSSID_0 : mvif->omac_idx;
	basic_req.basic.hw_bss_idx = idx;

	memcpy(dev_req.tlv.omac_addr, sc->sc_lladdr, ETHER_ADDR_LEN);

	if (enable) {
		cmd = MCU_UNI_CMD_DEV_INFO_UPDATE;
		data = &dev_req;
		len = sizeof(dev_req);
	} else {
		cmd = MCU_UNI_CMD_BSS_INFO_UPDATE;
		data = &basic_req;
		len = sizeof(basic_req);
	}

printf("%s: %s cmd %x mvif idx %d omac %d band %d wmm %d\n", DEVNAME(sc), __func__, cmd, mvif->idx, mvif->omac_idx, mvif->band_idx, mvif->wmm_idx);
	rv = mwx_mcu_send_wait(sc, cmd, data, len);
	if (rv < 0)
		return rv;

	if (enable) {
		cmd = MCU_UNI_CMD_BSS_INFO_UPDATE;
		data = &basic_req;
		len = sizeof(basic_req);
	} else {
		cmd = MCU_UNI_CMD_DEV_INFO_UPDATE;
		data = &dev_req;
		len = sizeof(dev_req);
	}

printf("%s: %s cmd %x wcid %d\n", DEVNAME(sc), __func__, cmd, mn->wcid);
	return mwx_mcu_send_wait(sc, cmd, data, len);
}

int
mt7921_mcu_set_sniffer(struct mwx_softc *sc, int enable)
{
	struct {
		uint8_t		band_idx;
		uint8_t		pad[3];
		struct sniffer_enable_tlv {
			uint16_t	tag;
			uint16_t	len;
			uint8_t		enable;
			uint8_t		pad[3];
		}		enable;
	} req = {
		.band_idx = 0,
		.enable = {
			.tag = htole16(0),
			.len = htole16(sizeof(req.enable)),
			.enable = (uint8_t)enable,
		},
	};

	return mwx_mcu_send_wait(sc, MCU_UNI_CMD_SNIFFER, &req, sizeof(req));
}

int
mt7921_mcu_set_beacon_filter(struct mwx_softc *sc, int enable)
{
	int rv;

	if (enable) {
#ifdef NOTYET
		rv = mt7921_mcu_uni_bss_bcnft(dev, vif, true);
		if (rv)
			return rv;
#endif
		mwx_set(sc, MT_WF_RFCR(0), MT_WF_RFCR_DROP_OTHER_BEACON);
	} else {
		rv = mt7921_mcu_set_bss_pm(sc, 0);
		if (rv)
			return rv;
		mwx_clear(sc, MT_WF_RFCR(0), MT_WF_RFCR_DROP_OTHER_BEACON);
	}
	return 0;
}

int
mt7921_mcu_set_bss_pm(struct mwx_softc *sc, int enable)
{
#ifdef NOTYET
	struct {
		uint8_t		bss_idx;
		uint8_t		dtim_period;
		uint16_t	aid;
		uint16_t	bcn_interval;
		uint16_t	atim_window;
		uint8_t		uapsd;
		uint8_t		bmc_delivered_ac;
		uint8_t		bmc_triggered_ac;
		uint8_t		pad;
	} req = {
		.bss_idx = mvif->mt76.idx,
		.aid = htole16(vif->cfg.aid),
		.dtim_period = vif->bss_conf.dtim_period,
		.bcn_interval = htole16(vif->bss_conf.beacon_int),
	};
#endif
	struct {
		uint8_t		bss_idx;
		uint8_t		pad[3];
	} req_hdr = {
		.bss_idx = /* mvif->mt76.idx XXX */ 0,
	};
	int rv;

	rv = mwx_mcu_send_msg(sc, MCU_CE_CMD_SET_BSS_ABORT,
		&req_hdr, sizeof(req_hdr), NULL);
#ifdef NOTYET
	if (rv != 0 || !enable)
		return rv;
	rv = mwx_mcu_send_msg(sc, MCU_CE_CMD_SET_BSS_CONNECTED,
		&req, sizeof(req), NULL);
#endif
	return rv;
}

#define IEEE80211_NUM_ACS	4
int
mt7921_mcu_set_tx(struct mwx_softc *sc, struct mwx_vif *mvif)
{
	struct edca {
		uint16_t	cw_min;
		uint16_t	cw_max;
		uint16_t	txop;
		uint16_t	aifs;
		uint8_t		guardtime;
		uint8_t		acm;
	} __packed;
	struct mt7921_mcu_tx {
		struct edca	edca[IEEE80211_NUM_ACS];
		uint8_t		bss_idx;
		uint8_t		qos;
		uint8_t		wmm_idx;
		uint8_t		pad;
	} __packed req = {
		.bss_idx = mvif->idx,
		.qos = /* vif->bss_conf.qos */ 0,
		.wmm_idx = mvif->wmm_idx,
	};
#ifdef NOTYET
	struct mu_edca {
		uint8_t		cw_min;
		uint8_t		cw_max;
		uint8_t		aifsn;
		uint8_t		acm;
		uint8_t		timer;
		uint8_t		padding[3];
	};
	struct mt7921_mcu_mu_tx {
		uint8_t		ver;
		uint8_t		pad0;
		uint16_t	len;
		uint8_t		bss_idx;
		uint8_t		qos;
		uint8_t		wmm_idx;
		uint8_t		pad1;
		struct mu_edca	edca[IEEE80211_NUM_ACS];
		uint8_t		pad3[32];
	} __packed req_mu = {
		.bss_idx = mvif->mt76.idx,
		.qos = vif->bss_conf.qos,
		.wmm_idx = mvif->mt76.wmm_idx,
	};
#endif
	static const int to_aci[] = { 1, 0, 2, 3 };
	int ac, rv;

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		//struct ieee80211_tx_queue_params *q = &mvif->queue_params[ac];
		struct edca *e = &req.edca[to_aci[ac]];

		e->aifs = htole16(/* q->aifs */ 2);
		e->txop = htole16(/* q->txop */ 0);

#ifdef NOTYET
		if (q->cw_min)
			e->cw_min = htole16(q->cw_min);
		else
#endif
			e->cw_min = htole16(5);

#ifdef NOTYET
		if ( q->cw_max)
			e->cw_max = htole16(q->cw_max);
		else
#endif
			e->cw_max = htole16(10);
	}

	rv = mwx_mcu_send_msg(sc, MCU_CE_CMD_SET_EDCA_PARMS, &req,
	    sizeof(req), NULL);

#ifdef NOTYET
	if (rv)
		return rv;
	if (!vif->bss_conf.he_support)
		return 0;

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		struct ieee80211_he_mu_edca_param_ac_rec *q;
		struct mu_edca *e;

		if (!mvif->queue_params[ac].mu_edca)
			break;

		q = &mvif->queue_params[ac].mu_edca_param_rec;
		e = &(req_mu.edca[to_aci[ac]]);

		e->cw_min = q->ecw_min_max & 0xf;
		e->cw_max = (q->ecw_min_max & 0xf0) >> 4;
		e->aifsn = q->aifsn;
		e->timer = q->mu_edca_timer;
	}

	rv = mt76_mcu_send_msg(&dev->mt76, MCU_CE_CMD(SET_MU_EDCA_PARMS),
	    &req_mu, sizeof(req_mu), false);
#endif
	return rv;
}

int
mt7921_mac_fill_rx(struct mwx_softc *sc, struct mbuf *m,
    struct ieee80211_rxinfo *rxi)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t *rxd, *rxv = NULL;
	uint32_t rxd0, rxd1, rxd2, rxd3, rxd4;
//	uint32_t mode = 0;
	uint16_t hdr_gap /*, seq_ctrl = 0, fc = 0 */;
	uint8_t chfnum, remove_pad /*, qos_ctl = 0, amsdu_info */;
	int idx, unicast, num_rxd = 6;
	int i;
//	bool insert_ccmp_hdr = false;

	if (mbuf_len(m) < num_rxd * sizeof(uint32_t)) {
		if (sc->sc_rx_fill_fail < 64) {
			MWX_DEV_LOG(sc,
			    "rx drop reason=short-rxd len=%zu need=%zu\n",
			    mbuf_len(m), num_rxd * sizeof(uint32_t));
		}
		return -1;
	}

	rxd = mtod(m, uint32_t *);
	rxd0 = le32toh(rxd[0]);
	rxd1 = le32toh(rxd[1]);
	rxd2 = le32toh(rxd[2]);
	rxd3 = le32toh(rxd[3]);
	rxd4 = le32toh(rxd[4]);

	if (rxd1 & MT_RXD1_NORMAL_BAND_IDX) {
		MWX_RX_DROP_LOG(sc, "band-idx", -1, rxd0, rxd1, rxd2,
		    rxd3, rxd4);
		return -1;
	}

	if (rxd2 & MT_RXD2_NORMAL_AMSDU_ERR) {
		MWX_RX_DROP_LOG(sc, "amsdu-err", -1, rxd0, rxd1, rxd2,
		    rxd3, rxd4);
		return -1;
	}

	if (rxd2 & MT_RXD2_NORMAL_HDR_TRANS) {
		MWX_RX_DROP_LOG(sc, "hdr-trans", -1, rxd0, rxd1, rxd2,
		    rxd3, rxd4);
		return -1;
	}

	/* ICV error or CCMP/BIP/WPI MIC error */
	if (rxd1 & MT_RXD1_NORMAL_ICV_ERR) {
		MWX_RX_DROP_LOG(sc, "icv", -1, rxd0, rxd1, rxd2, rxd3,
		    rxd4);
		ic->ic_stats.is_rx_decryptcrc++;
		return -1;
	}

	if (rxd1 & MT_RXD1_NORMAL_FCS_ERR) {
		MWX_RX_DROP_LOG(sc, "fcs", -1, rxd0, rxd1, rxd2, rxd3,
		    rxd4);
		return -1;
	}

	if (rxd1 & MT_RXD1_NORMAL_TKIP_MIC_ERR) {
		/* report MIC failures to net80211 for TKIP */
		MWX_RX_DROP_LOG(sc, "tkip-mic", -1, rxd0, rxd1, rxd2,
		    rxd3, rxd4);
		ic->ic_stats.is_rx_locmicfail++;
		ieee80211_michael_mic_failure(ic, 0/* XXX */);
		return -1;
	}


	chfnum = (rxd3 & MT_RXD3_NORMAL_CH_NUM_MASK) >>
	    MT_RXD3_NORMAL_CH_NUM_SHIFT;
	unicast = (rxd3 & MT_RXD3_NORMAL_ADDR_TYPE_MASK) == MT_RXD3_NORMAL_U2M;
	idx = rxd1 & MT_RXD1_NORMAL_WLAN_IDX_MASK;

#if 0
	status->wcid = mt7921_rx_get_wcid(dev, idx, unicast);
	if (status->wcid) {
		struct mt7921_sta *msta;

		msta = container_of(status->wcid, struct mt7921_sta, wcid);
		spin_lock_bh(&dev->sta_poll_lock);
		if (list_empty(&msta->poll_list))
			list_add_tail(&msta->poll_list, &dev->sta_poll_list);
		spin_unlock_bh(&dev->sta_poll_lock);
	}
#endif

#if NOTYET
	if ((rxd0 & (MT_RXD0_NORMAL_IP_SUM | MT_RXD0_NORMAL_UDP_TCP_SUM)) ==
	    (MT_RXD0_NORMAL_IP_SUM | MT_RXD0_NORMAL_UDP_TCP_SUM)) {
		m->m_pkthdr.csum_flags = M_IPV4_CSUM_IN_OK |
		    M_TCP_CSUM_IN_OK | M_UDP_CSUM_IN_OK;
	}
#endif

	if ((rxd1 & MT_RXD1_NORMAL_SEC_MODE_MASK) != 0 &&
	    !(rxd1 & (MT_RXD1_NORMAL_CLM | MT_RXD1_NORMAL_CM))) {
		rxi->rxi_flags |= IEEE80211_RXI_HWDEC;
	} else if ((rxd1 & MT_RXD1_NORMAL_SEC_MODE_MASK) != 0) {
		/* YYavic dbg 30: hardware saw a cipher but did not decrypt */
		if (rxd1 & MT_RXD1_NORMAL_CM)
			gYYRxCM++;
		if (rxd1 & MT_RXD1_NORMAL_CLM)
			gYYRxCLM++;
		gYYRxLastRxd1 = rxd1;
	}

	remove_pad = (rxd2 & MT_RXD2_NORMAL_HDR_OFFSET_MASK) >>
	    MT_RXD2_NORMAL_HDR_OFFSET_SHIFT;

	if (rxd2 & MT_RXD2_NORMAL_MAX_LEN_ERROR) {
		MWX_RX_DROP_LOG(sc, "max-len", -EINVAL, rxd0, rxd1, rxd2,
		    rxd3, rxd4);
		return -EINVAL;
	}

	rxd += 6;

	if (rxd1 & MT_RXD1_NORMAL_GROUP_4) {
		num_rxd += 4;
		rxd += 4;
	}
	if (rxd1 & MT_RXD1_NORMAL_GROUP_1) {
		num_rxd += 4;
		rxd += 4;
	}
	if (rxd1 & MT_RXD1_NORMAL_GROUP_2) {
		num_rxd += 2;
		rxd += 2;
	}
	if (rxd1 & MT_RXD1_NORMAL_GROUP_3) {	
		uint32_t v0, v1;
		int8_t chain[4], signal;

		rxv = rxd;
		num_rxd += 2;
		rxd += 2;

		v0 = le32toh(rxv[0]); /* XXX not implemented yet */ 
		v1 = le32toh(rxv[1]);
		{
			uint32_t md = (v0 >> 24) & 0xf, rt = v0 & 0x7f;
			gRxG3++;
			gRxMode[md]++;
			if ((md == 2 || md == 3) && rt < 32)	/* HT_MM / HT_GF */
				gRxHtMcs[rt]++;
			if (md == 1)	/* OFDM: rate kodu */
				gRxOfdm[rt & 0xf]++;
			if (md == 4) {	/* VHT: mcs = rate[3:0], nss = NSTS[9:7] + 1 */
				uint32_t nss = ((v0 >> 7) & 7);
				if (nss > 1) nss = 1;
				gRxVht[nss][(rt & 0xf) < 10 ? (rt & 0xf) : 9]++;
			}
			if ((v0 >> 15) & 3)
				gRxSgi++;
			gRxBw[(v0 >> 12) & 3]++;
			if (md == 2 || md == 3 || md == 4) {
				gYYRxMcs = md == 4 ? (rt & 0xf) : (rt & 0x1f);
				gYYRxNss = md == 4 ? (((v0 >> 7) & 7) + 1) : ((rt & 0x1f) / 8 + 1);
				gYYRxBw = (v0 >> 12) & 3;
				gYYRxSgi = ((v0 >> 15) & 3) ? 1 : 0;
				gYYRxMode = md;
			} else if (md >= 8 && md <= 11) {
				/* YYavic HE (SU 8, EXT_SU 9, TB 10, MU 11): mcs = rate[3:0],
				 * nss = NSTS + 1, GI code 0/1/2 = 0.8/1.6/3.2 us */
				gYYRxHe++;
				gYYRxHeMcs[rt & 0xf]++;
				gYYRxMcs = rt & 0xf;
				gYYRxNss = ((v0 >> 7) & 7) + 1;
				gYYRxBw = (v0 >> 12) & 3;
				gYYRxSgi = (v0 >> 15) & 3;
				gYYRxMode = md;
			}
		}

		if (rxd1 & MT_RXD1_NORMAL_GROUP_5) {
			num_rxd += 18;
			rxd += 6;
			rxv = rxd;
			v1 = le32toh(rxv[0]); 
			rxd += 12;
		}
	

		chain[0] = rcpi_to_rssi(MT_PRXV_RCPI0_SHIFT, v1);
		chain[1] = rcpi_to_rssi(MT_PRXV_RCPI1_SHIFT, v1);
		chain[2] = rcpi_to_rssi(MT_PRXV_RCPI2_SHIFT, v1);
		chain[3] = rcpi_to_rssi(MT_PRXV_RCPI3_SHIFT, v1);

		signal = -128;
		for (i = 0; i < sc->sc_capa.num_streams; i++) {
			if (!(sc->sc_capa.antenna_mask & (1U << i)))
				continue;	
			if (chain[i] >= 0)
				continue;
			signal = MAX(signal, chain[i]);
		}
		rxi->rxi_rssi = signal;
		}

	if (mbuf_len(m) < num_rxd * sizeof(uint32_t)) {
		if (sc->sc_rx_fill_fail < 64) {
			MWX_DEV_LOG(sc,
			    "rx drop reason=short-groups len=%zu need=%zu rxd=%08x/%08x/%08x/%08x/%08x\n",
			    mbuf_len(m), num_rxd * sizeof(uint32_t),
			    rxd0, rxd1, rxd2, rxd3, rxd4);
		}
		return -1;
	}

#if 0
	if (rxd1 & MT_RXD1_NORMAL_GROUP_4) {
		uint32_t v0 = le32toh(rxd[0]);
		uint32_t v2 = le32toh(rxd[2]);

		fc = htole16(FIELD_GET(MT_RXD6_FRAME_CONTROL, v0));
		seq_ctrl = FIELD_GET(MT_RXD8_SEQ_CTRL, v2);
		qos_ctl = FIELD_GET(MT_RXD8_QOS_CTL, v2);

		rxd += 4;
	}

	if (rxd1 & MT_RXD1_NORMAL_GROUP_1) {
		u8 *data = (u8 *)rxd;

		if (status->flag & RX_FLAG_DECRYPTED) {
			switch (FIELD_GET(MT_RXD1_NORMAL_SEC_MODE, rxd1)) {
			case MT_CIPHER_AES_CCMP:
			case MT_CIPHER_CCMP_CCX:
			case MT_CIPHER_CCMP_256:
				insert_ccmp_hdr =
					(rxd2 & MT_RXD2_NORMAL_FRAG);
				/* FALLTHROUGH */
			case MT_CIPHER_TKIP:
			case MT_CIPHER_TKIP_NO_MIC:
			case MT_CIPHER_GCMP:
			case MT_CIPHER_GCMP_256:
				status->iv[0] = data[5];
				status->iv[1] = data[4];
				status->iv[2] = data[3];
				status->iv[3] = data[2];
				status->iv[4] = data[1];
				status->iv[5] = data[0];
				break;
			default:
				break;
			}
		}
		rxd += 4;
	}

	if (rxd1 & MT_RXD1_NORMAL_GROUP_2) {
		status->timestamp = le32_to_cpu(rxd[0]);
		status->flag |= RX_FLAG_MACTIME_START;

		if (!(rxd2 & MT_RXD2_NORMAL_NON_AMPDU)) {
			status->flag |= RX_FLAG_AMPDU_DETAILS;

			/* all subframes of an A-MPDU have the same timestamp */
			if (phy->rx_ampdu_ts != status->timestamp) {
				if (!++phy->ampdu_ref)
					phy->ampdu_ref++;
			}
			phy->rx_ampdu_ts = status->timestamp;

			status->ampdu_ref = phy->ampdu_ref;
		}

		rxd += 2;
	}

	/* RXD Group 3 - P-RXV */
	if (rxd1 & MT_RXD1_NORMAL_GROUP_3) {
		u8 stbc, gi;
		u32 v0, v1;
		bool cck;

		rxv = rxd;
		rxd += 2;

		v0 = le32_to_cpu(rxv[0]);
		v1 = le32_to_cpu(rxv[1]);

		if (v0 & MT_PRXV_HT_AD_CODE)
			status->enc_flags |= RX_ENC_FLAG_LDPC;

		status->chains = mphy->antenna_mask;
		status->chain_signal[0] = to_rssi(MT_PRXV_RCPI0, v1);
		status->chain_signal[1] = to_rssi(MT_PRXV_RCPI1, v1);
		status->chain_signal[2] = to_rssi(MT_PRXV_RCPI2, v1);
		status->chain_signal[3] = to_rssi(MT_PRXV_RCPI3, v1);
		status->signal = -128;
		for (i = 0; i < hweight8(mphy->antenna_mask); i++) {
			if (!(status->chains & BIT(i)) ||
			    status->chain_signal[i] >= 0)
				continue;

			status->signal = max(status->signal,
					     status->chain_signal[i]);
		}

		stbc = FIELD_GET(MT_PRXV_STBC, v0);
		gi = FIELD_GET(MT_PRXV_SGI, v0);
		cck = false;

		idx = i = FIELD_GET(MT_PRXV_TX_RATE, v0);
		mode = FIELD_GET(MT_PRXV_TX_MODE, v0);

		switch (mode) {
		case MT_PHY_TYPE_CCK:
			cck = true;
			fallthrough;
		case MT_PHY_TYPE_OFDM:
			i = mt76_get_rate(&dev->mt76, sband, i, cck);
			break;
		case MT_PHY_TYPE_HT_GF:
		case MT_PHY_TYPE_HT:
			status->encoding = RX_ENC_HT;
			if (i > 31)
				return -EINVAL;
			break;
		case MT_PHY_TYPE_VHT:
			status->nss =
				FIELD_GET(MT_PRXV_NSTS, v0) + 1;
			status->encoding = RX_ENC_VHT;
			if (i > 9)
				return -EINVAL;
			break;
		case MT_PHY_TYPE_HE_MU:
		case MT_PHY_TYPE_HE_SU:
		case MT_PHY_TYPE_HE_EXT_SU:
		case MT_PHY_TYPE_HE_TB:
			status->nss =
				FIELD_GET(MT_PRXV_NSTS, v0) + 1;
			status->encoding = RX_ENC_HE;
			i &= GENMASK(3, 0);

			if (gi <= NL80211_RATE_INFO_HE_GI_3_2)
				status->he_gi = gi;

			status->he_dcm = !!(idx & MT_PRXV_TX_DCM);
			break;
		default:
			return -EINVAL;
		}

		status->rate_idx = i;

		switch (FIELD_GET(MT_PRXV_FRAME_MODE, v0)) {
		case IEEE80211_STA_RX_BW_20:
			break;
		case IEEE80211_STA_RX_BW_40:
			if (mode & MT_PHY_TYPE_HE_EXT_SU &&
			    (idx & MT_PRXV_TX_ER_SU_106T)) {
				status->bw = RATE_INFO_BW_HE_RU;
				status->he_ru =
					NL80211_RATE_INFO_HE_RU_ALLOC_106;
			} else {
				status->bw = RATE_INFO_BW_40;
			}
			break;
		case IEEE80211_STA_RX_BW_80:
			status->bw = RATE_INFO_BW_80;
			break;
		case IEEE80211_STA_RX_BW_160:
			status->bw = RATE_INFO_BW_160;
			break;
		default:
			return -EINVAL;
		}

		status->enc_flags |= RX_ENC_FLAG_STBC_MASK * stbc;
		if (mode < MT_PHY_TYPE_HE_SU && gi)
			status->enc_flags |= RX_ENC_FLAG_SHORT_GI;

		if (rxd1 & MT_RXD1_NORMAL_GROUP_5) {
			rxd += 18;
		}
	}

	amsdu_info = FIELD_GET(MT_RXD4_NORMAL_PAYLOAD_FORMAT, rxd4);
	status->amsdu = !!amsdu_info;
	if (status->amsdu) {
		status->first_amsdu = amsdu_info == MT_RXD4_FIRST_AMSDU_FRAME;
		status->last_amsdu = amsdu_info == MT_RXD4_LAST_AMSDU_FRAME;
	}
#endif

	hdr_gap = num_rxd * sizeof(uint32_t) + 2 * remove_pad;
	if (mbuf_len(m) < hdr_gap + sizeof(struct ieee80211_frame_min)) {
		if (sc->sc_rx_fill_fail < 64) {
			MWX_DEV_LOG(sc,
			    "rx drop reason=short-80211 len=%zu hdr_gap=%u rxd=%08x/%08x/%08x/%08x/%08x\n",
			    mbuf_len(m), hdr_gap, rxd0, rxd1, rxd2, rxd3,
			    rxd4);
		}
		return -1;
	}
	m_adj(m, hdr_gap);
#if 0
	if (status->amsdu) {
		memmove(skb->data + 2, skb->data,
			ieee80211_get_hdrlen_from_skb(skb));
		skb_pull(skb, 2);
	}

	struct ieee80211_hdr *hdr;

	if (insert_ccmp_hdr) {
		u8 key_id = FIELD_GET(MT_RXD1_NORMAL_KEY_ID, rxd1);

		mt76_insert_ccmp_hdr(skb, key_id);
	}

	hdr = mt76_skb_get_hdr(skb);
	fc = hdr->frame_control;
	if (ieee80211_is_data_qos(fc)) {
		seq_ctrl = le16_to_cpu(hdr->seq_ctrl);
		qos_ctl = *ieee80211_get_qos_ctl(hdr);
	}

	if (!status->wcid || !ieee80211_is_data_qos(fc))
		return 0;

	status->aggr = unicast && !ieee80211_is_qos_nullfunc(fc);
	status->seqno = IEEE80211_SEQ_TO_SN(seq_ctrl);
	status->qos_ctl = qos_ctl;
#endif
	rxi->rxi_chan = chfnum;

	if (rxi->rxi_flags & IEEE80211_RXI_HWDEC) {
		/*
		 * MT7921/MT7922 firmware decrypts and removes CCMP IV/MIC.
		 * Leave the protected bit clear so net80211 does not try to
		 * strip an IV which is no longer present.
		 */
		struct ieee80211_frame *wh;

		wh = mtod(m, struct ieee80211_frame *);
		wh->i_fc[1] &= ~IEEE80211_FC1_PROTECTED;
	}

	/*
	 * YYavic RX A-MSDU (replaces R9T trace28): the hardware A-MSDU splitter
	 * (MDP_DCR0_DAMSDU_EN) hands over every subframe as its own buffer:
	 * the 802.11 header (A-MSDU bit still set in QoS), 2 pad bytes, then
	 * one subframe (DA, SA, length, payload). As Linux mt7921_mac_fill_rx
	 * drop the 2 bytes; mwx_rx joins the subframes of one MPDU back
	 * together (same sequence number, RXD4 first/mid/last) so net80211
	 * reorders one MPDU and its A-MSDU decap splits it.
	 */
	if ((rxd4 & MT_RXD4_NORMAL_PAYLOAD_FORMAT) != 0) {
		struct ieee80211_frame *wh = mtod(m, struct ieee80211_frame *);
		uint8_t *d = mtod(m, uint8_t *);
		size_t plen = mbuf_len(m);
		u_int hdrlen = ieee80211_get_hdrlen(wh);
		u_int off, sublen;
		int fmt = rxd4 & MT_RXD4_NORMAL_PAYLOAD_FORMAT;

		if (!ieee80211_has_qos(wh) || plen < hdrlen + 14) {
			gYYAmsduBad++;
			return -1;
		}
		/* Linux always removes the 2 bytes; keep a 0-gap fallback. */
		if (plen >= hdrlen + 2 + 14 &&
		    memcmp(d + hdrlen + 2, wh->i_addr1, IEEE80211_ADDR_LEN) == 0)
			off = 2;
		else if (memcmp(d + hdrlen, wh->i_addr1, IEEE80211_ADDR_LEN) == 0)
			off = 0;
		else {
			off = 2;
			gYYAmsduNoDa++;
		}
		if (off) {
			memmove(d + off, d, hdrlen);
			m_adj(m, off);
			d = mtod(m, uint8_t *);
			wh = (struct ieee80211_frame *)d;
			plen = mbuf_len(m);
		}
		sublen = ((u_int)d[hdrlen + 12] << 8) | d[hdrlen + 13];
		if (hdrlen + 14 + sublen > plen) {
			gYYAmsduBad++;
			return -1;
		}
		if (plen > hdrlen + 14 + sublen)	/* trailing pad */
			m_adj(m, -(int)(plen - (hdrlen + 14 + sublen)));
		{
			u_int qoff = hdrlen - 2;
			if ((wh->i_fc[1] & IEEE80211_FC1_ORDER) && hdrlen >= 30)
				qoff -= 4;	/* HT Control after QoS */
			d[qoff] |= (IEEE80211_QOS_AMSDU & 0xff);
		}
		rxi->rxi_flags |= (uint32_t)fmt << MWX_RXI_AMSDU_SHIFT;
		gYYAmsduSub++;
	}

	return 0;
}

uint32_t
mt7921_mac_tx_rate_val(struct mwx_softc *sc)
{
	int rateidx = 0, offset = 4;
	uint32_t rate, mode;

	/* XXX TODO basic_rates
	rateidx = ffs(vif->bss_conf.basic_rates) - 1;
	*/

	if (IEEE80211_IS_CHAN_2GHZ(sc->sc_ic.ic_bss->ni_chan))
		offset = 0;
	/* pick the lowest rate for hidden nodes */
	if (rateidx < 0)
		rateidx = 0;

	rateidx += offset;

	if (rateidx >= nitems(mt76_rates))
		rateidx = offset;

	rate =  mt76_rates[rateidx].hw_value;
	mode = (rate >> 8) << MT_TX_RATE_MODE_SHIFT;
	rate &= 0xff;

	return (rate & MT_TX_RATE_IDX_MASK) | (mode & MT_TX_RATE_MODE_MASK);
}

void
mt7921_mac_write_txwi_80211(struct mwx_softc *sc, struct mbuf *m,
    struct ieee80211_node *ni, struct mt76_txwi *txp)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_frame *wh;
	uint32_t val;
	uint8_t type, subtype, tid = 0;
	u_int hdrlen;
	int multicast;


	wh = mtod(m, struct ieee80211_frame *);
	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
	type >>= IEEE80211_FC0_TYPE_SHIFT;
	subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;
	subtype >>= IEEE80211_FC0_SUBTYPE_SHIFT;
	multicast = IEEE80211_IS_MULTICAST(wh->i_addr1);

	/* R9T trace36: type kaydirilmis (0..3); sabitler kaydirilmamis (0x04/0x08) */
	if (type == (IEEE80211_FC0_TYPE_CTL >> IEEE80211_FC0_TYPE_SHIFT))
		hdrlen = sizeof(struct ieee80211_frame_min);
	else
		hdrlen = ieee80211_get_hdrlen(wh);

	/* Put QoS frames on the data queue which maps to their TID. */
	if (ieee80211_has_qos(wh)) {
		uint16_t qos = ieee80211_get_qos(wh);

		tid = qos & IEEE80211_QOS_TID;
	}

#ifdef NOTYET
	if (ieee80211_is_action(fc) &&
	    mgmt->u.action.category == WLAN_CATEGORY_BACK &&
	    mgmt->u.action.u.addba_req.action_code == WLAN_ACTION_ADDBA_REQ) {
		u16 capab = le16_to_cpu(mgmt->u.action.u.addba_req.capab);

		txp->txwi[5] |= htole32(MT_TXD5_ADD_BA);
		tid = (capab >> 2) & IEEE80211_QOS_CTL_TID_MASK;
	} else if (ieee80211_is_back_req(hdr->frame_control)) {
		struct ieee80211_bar *bar = (struct ieee80211_bar *)hdr;
		u16 control = le16_to_cpu(bar->control);

		tid = FIELD_GET(IEEE80211_BAR_CTRL_TID_INFO_MASK, control);
	}
#endif

	/* R9T trace37: ADDBA istegi -> TXD5_ADD_BA (Linux mt76_connac2_mac_write_txwi_80211) */
	if (type == (IEEE80211_FC0_TYPE_MGT >> IEEE80211_FC0_TYPE_SHIFT) &&
	    (wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) == IEEE80211_FC0_SUBTYPE_ACTION &&
	    mbuf_len(m) >= hdrlen + 5) {
		const uint8_t *frm = mtod(m, const uint8_t *) + hdrlen;
		if (frm[0] == IEEE80211_CATEG_BA && frm[1] == IEEE80211_ACTION_ADDBA_REQ) {
			uint16_t capab = frm[3] | (frm[4] << 8);
			txp->txwi[5] |= htole32(MT_TXD5_ADD_BA);
			tid = (capab >> 2) & 0xf;
		}
	}

	val = MT_HDR_FORMAT_802_11 | MT_TXD1_HDR_INFO(hdrlen / 2) |
	    MT_TXD1_TID(tid);
	txp->txwi[1] |= htole32(val);

	val = MT_TXD2_FRAME_TYPE(type) | MT_TXD2_SUB_TYPE(subtype);
	if (multicast)
		val |= MT_TXD2_MULTICAST;

#ifdef NOTYET
	if (key && multicast && ieee80211_is_robust_mgmt_frame(skb) &&
	    key->cipher == WLAN_CIPHER_SUITE_AES_CMAC) {
		val |= MT_TXD2_BIP;
		txp->txwi[3] &= ~htole32(MT_TXD3_PROTECT_FRAME);
	}
#endif

	/*
	 * R9T trace19: FW hiz uyarlamasi (STA_REC RA) kurulu degil; sabit-hizsiz unicast veri cerceveleri FW'de
	 * takiliyor gibi (TX DMA halkasi 11-12. girişte duruyor). Tum cerceveler icin sabit hiz kullan.
	 */
	/* trace26: kok neden (5+ parca TXP tasmasi) bulundu; unicast veride FW hiz uyarlamasi geri acik. */
	/*
	 * R9T trace36: eskiden type (kaydirilmis, veri=2) IEEE80211_FC0_TYPE_DATA (0x08) ile
	 * karsilastiriliyordu -> kosul HEP dogru -> tum cerceveler sabit en dusuk hiz
	 * (5 GHz 6 Mbps). Simdi yalniz multicast/veri-disi sabit hiz; unicast veri FW RA.
	 */
	if (multicast || type != (IEEE80211_FC0_TYPE_DATA >> IEEE80211_FC0_TYPE_SHIFT)) {
		/* Fixed rata is available just for 802.11 txd */
		uint32_t rate, val6;

		val |= MT_TXD2_FIX_RATE;
		/* hardware won't add HTC for mgmt/ctrl frame */
		val |= htole32(MT_TXD2_HTC_VLD);

		rate = mt7921_mac_tx_rate_val(sc);

		val6 = MT_TXD6_FIXED_BW;
		val6 |= (rate << MT_TXD6_TX_RATE_SHIFT) & MT_TXD6_TX_RATE_MASK;
		txp->txwi[6] |= htole32(val6);
		txp->txwi[3] |= htole32(MT_TXD3_BA_DISABLE);
	} else if (ni != NULL && ieee80211_has_qos(wh) && tid < IEEE80211_NUM_TID &&
	    ni->ni_tx_ba[tid].ba_state == IEEE80211_BA_AGREED) {
		/*
		 * R9T trace37: anlasmali TID -> toplama acik; sira numarasini net80211'inki
		 * ile sabitle (ADDBA ssn ile uyumlu).
		 */
		uint16_t sn = le16toh(*(uint16_t *)wh->i_seq) >> IEEE80211_SEQ_SEQ_SHIFT;
		txp->txwi[3] |= htole32(MT_TXD3_SN_VALID | ((uint32_t)(sn & 0xfff) << 16));
	} else {
		/* R9T trace27: TX Block Ack oturumu yok */
		txp->txwi[3] |= htole32(MT_TXD3_BA_DISABLE);
	}

	txp->txwi[2] |= htole32(val);

#ifdef NOTYET
	if (ieee80211_is_beacon(fc)) {
		txp->txwi[3] &= ~htole32(MT_TXD3_SW_POWER_MGMT);
		txp->txwi[3] |= htole32(MT_TXD3_REM_TX_COUNT);
	}

	if (info->flags & IEEE80211_TX_CTL_INJECTED) {
		u16 seqno = le16_to_cpu(hdr->seq_ctrl);

		if (ieee80211_is_back_req(hdr->frame_control)) {
			struct ieee80211_bar *bar;

			bar = (struct ieee80211_bar *)skb->data;
			seqno = le16_to_cpu(bar->start_seq_num);
		}

		val = MT_TXD3_SN_VALID |
			FIELD_PREP(MT_TXD3_SEQ, IEEE80211_SEQ_TO_SN(seqno));
		txp->txwi[3] |= htole32(val);
		txp->txwi[7] &= ~htole32(MT_TXD7_HW_AMSDU);
	}
#endif

	val = MT_TXD7_TYPE(type) | MT_TXD7_SUB_TYPE(subtype);
	txp->txwi[7] |= htole32(val);

#if NBPFILTER > 0
	if (__predict_false(sc->sc_drvbpf != NULL)) {
		struct mwx_tx_radiotap_header *tap = &sc->sc_txtap;
		uint16_t chan_flags;

		tap->wt_flags = 0;
		tap->wt_chan_freq = htole16(ni->ni_chan->ic_freq);
		chan_flags = ni->ni_chan->ic_flags;
		if (ic->ic_curmode != IEEE80211_MODE_11N &&
			ic->ic_curmode != IEEE80211_MODE_11AC) {
			chan_flags &= ~IEEE80211_CHAN_HT;
			chan_flags &= ~IEEE80211_CHAN_40MHZ;
		}
		if (ic->ic_curmode != IEEE80211_MODE_11AC)
			chan_flags &= ~IEEE80211_CHAN_VHT;
		tap->wt_chan_flags = htole16(chan_flags);
#ifdef NOTYET
		if ((ni->ni_flags & IEEE80211_NODE_HT) &&
		    !IEEE80211_IS_MULTICAST(wh->i_addr1) &&
		    type == IEEE80211_FC0_TYPE_DATA &&
		    rinfo->ht_plcp != MWX_RATE_HT_SISO_MCS_INV_PLCP) {
			tap->wt_rate = (0x80 | rinfo->ht_plcp);
		} else
			tap->wt_rate = rinfo->rate;
#endif
		tap->wt_rate = 2;
		if ((ic->ic_flags & IEEE80211_F_WEPON) &&
		    (wh->i_fc[1] & IEEE80211_FC1_PROTECTED))
			tap->wt_flags |= IEEE80211_RADIOTAP_F_WEP;

		bpf_mtap_hdr(sc->sc_drvbpf, tap, sc->sc_txtap_len,
		    m, BPF_DIRECTION_OUT);
	}
#endif

}

static inline uint8_t
mt7921_lmac_mapping(uint8_t ac)
{
	/* LMAC uses the reverse order of mac80211 AC indexes */
	return 3 - ac;
}

void
mt7921_mac_write_txwi(struct mwx_softc *sc, struct mbuf *m,
    struct ieee80211_node *ni, struct mt76_txwi *txp)
{
	struct mwx_node *mn = (struct mwx_node *)ni;
	struct ieee80211_frame *wh;
	uint32_t val, p_fmt, omac_idx;
	uint8_t q_idx, wmm_idx, band_idx;
	uint8_t phy_idx = 0;
	/* XXX hardcoded and wrong */
	int pid = MT_PACKET_ID_FIRST;
	enum mt76_txq_id qid = MT_TXQ_BE;

	wh = mtod(m, struct ieee80211_frame *);
	omac_idx = sc->sc_vif.omac_idx << MT_TXD1_OWN_MAC_SHIFT;
	wmm_idx = sc->sc_vif.wmm_idx;
	band_idx = sc->sc_vif.band_idx;

	if (qid >= MT_TXQ_PSD) {
		p_fmt = MT_TX_TYPE_CT;
		q_idx = MT_LMAC_ALTX0;
	} else {
		p_fmt = MT_TX_TYPE_CT;
		/*
		 * R9T: eskiden her cerceve mac80211 kuyruk 0 (= VO, LMAC AC03) ile gidiyordu. trace16: veri TX halkasi
		 * 11. girisinde takiliyor (dma=11, cpu>=41), AP reason 37 ile disassoc ediyor. Veri cercevelerini TID/AC'ye
		 * gore (QoS yoksa BE) sec; yonetim cerceveleri VO'da kalir.
		 */
		{
			uint8_t ac = 0;	/* mac80211 AC: 0=VO 1=VI 2=BE 3=BK */
			struct ieee80211_frame *qwh = mtod(m, struct ieee80211_frame *);
			if ((qwh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) == IEEE80211_FC0_TYPE_DATA) {
				ac = 2;
				if (ieee80211_has_qos(qwh)) {
					uint8_t qtid = ieee80211_get_qos(qwh) & IEEE80211_QOS_TID;
					static const uint8_t tid2ac[8] = { 2, 3, 3, 2, 1, 1, 0, 0 };
					ac = tid2ac[qtid & 7];
				}
			}
			q_idx = wmm_idx * MWX_MAX_WMM_SETS + mt7921_lmac_mapping(ac);
		}

#ifdef NOTYET
		/* counting non-offloading skbs */
		wcid->stats.tx_bytes += skb->len;
		wcid->stats.tx_packets++;
#endif
	}

	val = ((mbuf_pkthdr_len(m) + MT_TXD_SIZE) & MT_TXD0_TX_BYTES_MASK) |
		p_fmt | MT_TXD0_Q_IDX(q_idx);
	txp->txwi[0] = htole32(val);

	val = MT_TXD1_LONG_FORMAT | (mn->wcid & MT_TXD1_WLAN_IDX_MASK) |
	    (omac_idx & MT_TXD1_OWN_MAC_MASK);
	if (phy_idx || band_idx)
		val |= MT_TXD1_TGID;
	txp->txwi[1] = htole32(val);
	txp->txwi[2] = 0;

	val = 15 << MT_TXD3_REM_TX_COUNT_SHIFT;
	if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED)
		val |= MT_TXD3_PROTECT_FRAME;
#ifdef NOTYET
	if (info->flags & IEEE80211_TX_CTL_NO_ACK)
		val |= MT_TXD3_NO_ACK;
#endif
	txp->txwi[3] = htole32(val);
	txp->txwi[4] = 0;

	val = pid & MT_TXD5_PID;
	if (pid >= MT_PACKET_ID_FIRST)
		val |= MT_TXD5_TX_STATUS_HOST;
	txp->txwi[5] = htole32(val);
	txp->txwi[6] = 0;
	txp->txwi[7] = /* XXX wcid->amsdu ? htole32(MT_TXD7_HW_AMSDU) : */ 0;

#ifdef NOTYET
	if (is_8023)
		mt76_connac2_mac_write_txwi_8023(txp, m, wcid);
	else
#endif
		mt7921_mac_write_txwi_80211(sc, m, ni, txp);
}

/* R9T trace34: kaynak bosaldiysa OACTIVE'i kaldirip gonderime devam et */
static void
mwx_tx_kick(struct mwx_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	if (!ifq_is_oactive(&ifp->if_snd))
		return;
	if (LIST_EMPTY(&sc->sc_txwi.mt_freelist) ||
	    mwx_dma_free_slots(&sc->sc_txq) < 2)
		return;
	ifq_clr_oactive(&ifp->if_snd);
	gTxRestart++;
	mwx_start(ifp);
}

void
mwx_mac_tx_free(struct mwx_softc *sc, struct mbuf *m)
{
	struct mwx_txwi *mt;
	uint32_t *txfree;
	uint32_t txval;
	int count, i;

	mwx_dma_tx_cleanup(sc, &sc->sc_txq);

	/* Both the event header and each PAIR/MSDU word must be present. */
	if (mbuf_pkthdr_len(m) < 2 * sizeof(txval))
		goto out;

	if ((m = m_pullup(m, mbuf_pkthdr_len(m))) == NULL)
		return;

	txfree = mtod(m, uint32_t *);
	txval = le32toh(txfree[0]);
	m_adj(m, 2 * sizeof(txval));

	count = MT_TX_FREE0_MSDU_CNT_GET(txval);

	if (count * sizeof(txval) > mbuf_len(m))
		goto out;

	txfree = mtod(m, uint32_t *);
	for (i = 0; i < count; i++) {
		uint16_t msdu;

		/* PAIR words extend count, but never the received payload. */
		if ((size_t)i >= mbuf_len(m) / sizeof(*txfree))
			break;
		txval = le32toh(txfree[i]);
		if (txval & MT_TX_FREE_PAIR) {
			count++;
			continue;
		}

		msdu = MT_TX_FREE_MSDU_ID_GET(txval);
		if (msdu >= sc->sc_txwi.mt_count)
			continue;
		mt = &sc->sc_txwi.mt_data[msdu];
		if (mt->mt_busy == 0)
			continue;
		mwx_txwi_put(sc, mt);
		gTxFreed++;
	}

out:
	gTxFreeEv++;
	m_freem(m);
	mwx_tx_kick(sc);
}

int
mt7921_set_channel(struct mwx_softc *sc)
{
	int rv;

	/* stop queues, block other configs (MT76_RESET) */
	// XXX NOTYET mt76_set_channel(sc);

	rv = mt7921_mcu_set_chan_info(sc, MCU_EXT_CMD_CHANNEL_SWITCH);
	if (rv)
		return rv;
	/* R9T trace30: Linux __mt7921_start'taki RX yolu (anten/akis) yapilandirmasi; STA'da hic gonderilmiyordu */
	{
		int rrv = mt7921_mcu_set_chan_info(sc, MCU_EXT_CMD_SET_RX_PATH);
		static uint32_t n;
		if (n++ < 8)
			MWX_DEV_LOG(sc, "set_channel SET_RX_PATH rv=%d streams=%u mask=0x%x\n",
			    rrv, sc->sc_capa.num_streams, sc->sc_capa.antenna_mask);
	}
	mt7921_mac_set_timing(sc);
	mt7921_mac_reset_counters(sc);

	/* restart queues */
	return 0;
}

uint8_t
mt7921_get_phy_mode_v2(struct mwx_softc *sc, struct ieee80211_node *ni)
{
	uint8_t mode = 0;

	if (ni == NULL)
		ni = sc->sc_ic.ic_bss;

	if (IEEE80211_IS_CHAN_2GHZ(ni->ni_chan)) {
		mode |= PHY_TYPE_BIT_HR_DSSS | PHY_TYPE_BIT_ERP;
		if (ieee80211_node_supports_ht(ni))
			mode |= PHY_TYPE_BIT_HT;
		if (ni->ni_flags & IEEE80211_NODE_HE)	/* YYavic HE */
			mode |= PHY_TYPE_BIT_HE;
	} else if (IEEE80211_IS_CHAN_5GHZ(ni->ni_chan) /* || CHAN_6GHZ */) {
		mode |= PHY_TYPE_BIT_OFDM;
		if (ieee80211_node_supports_ht(ni))
			mode |= PHY_TYPE_BIT_HT;
		if (ieee80211_node_supports_vht(ni))
			mode |= PHY_TYPE_BIT_VHT;
		if (ni->ni_flags & IEEE80211_NODE_HE)	/* YYavic HE */
			mode |= PHY_TYPE_BIT_HE;
	}
	return mode;
}

struct mbuf *
mwx_alloc_sta_req_tlv(int len)
{
	struct mbuf *m;

	/* Allocate mbuf cluster with enough space */
	m = MCLGETL(NULL, M_DONTWAIT, MCLBYTES);
	if (m == NULL)
		return NULL;

	/* align to have space for the mcu header */
	mbuf_setdata(m, (uint8_t *)mbuf_data(m) + sizeof(struct mwx_mcu_txd) + len, 0);
	mbuf_setlen(m, 0);
	mbuf_pkthdr_setlen(m, 0);

	return m;
}

void *
mwx_append_len(struct mbuf *m, int len)
{
	caddr_t p;
	size_t oldlen;

	KASSERT(m_trailingspace(m) >= len);

	oldlen = mbuf_len(m);
	p = mtod(m, caddr_t) + oldlen;
	mbuf_setlen(m, oldlen + len);
	mbuf_pkthdr_setlen(m, mbuf_len(m));
	memset(p, 0, len);

	return p;
}

/*
 * Reserve len bytes at the end of mbuf m, return to start of that area
 * after initializing the data. It also sets the tag and len hdr.
 */
void *
mwx_append_tlv(struct mbuf *m, uint16_t *tlvnum, int tag, int len)
{
	struct {
		uint16_t	tag;
		uint16_t	len;
	} tlv = {
		.tag = htole16(tag),
		.len = htole16(len),
	};
	caddr_t p;

	p = (caddr_t)mwx_append_len(m, len);
	memcpy(p, &tlv, sizeof(tlv));

	*tlvnum += 1;

	return p;
}

struct mbuf *
mwx_fill_sta_req_hdr(struct mbuf *m, struct mwx_vif *mvif,
    uint8_t muar_idx, uint16_t wcid, uint16_t tlvnum)
{
	struct sta_req_hdr *hdr;

	KASSERT(m_leadingspace(m) >= sizeof(struct sta_req_hdr));
	m = m_prepend(m, sizeof(struct sta_req_hdr), M_DONTWAIT);
	if (m == NULL)
		return NULL;
	hdr = mtod(m, struct sta_req_hdr *);
	memset(hdr, 0, sizeof(struct sta_req_hdr));

	hdr->bss_idx = mvif->idx;
	hdr->wlan_idx_lo = wcid & 0xff;
	hdr->wlan_idx_hi = wcid >> 8;
	hdr->muar_idx = muar_idx;
	hdr->is_tlv_append = 1;
	hdr->tlv_num = htole16(tlvnum);
	return m;
}

void
mt7921_mcu_add_basic_tlv(struct mbuf *m, uint16_t *tlvnum, struct mwx_softc *sc,
    struct ieee80211_node *ni, int add, int isnew)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct sta_rec_basic *basic;

	basic = (struct sta_rec_basic *)mwx_append_tlv(m, tlvnum,
	    STA_REC_BASIC, sizeof(*basic));

	basic->extra_info = htole16(EXTRA_INFO_VER);
	if (add) {
		if (isnew)
			basic->extra_info |= htole16(EXTRA_INFO_NEW);
		basic->conn_state = CONN_STATE_PORT_SECURE;
	} else {
		basic->conn_state = CONN_STATE_DISCONNECT;
	}

	if (ni == NULL) {
		basic->conn_type = htole32(STA_TYPE_BC | NETWORK_INFRA);
		memset(basic->peer_addr, 0xff, sizeof(basic->peer_addr));
		return;
	}

	switch (ic->ic_opmode) {
#ifndef IEEE80211_STA_ONLY
	case IEEE80211_M_HOSTAP:
		basic->conn_type = htole32(STA_TYPE_STA | NETWORK_INFRA);
		break;
#endif
	case IEEE80211_M_STA:
		basic->conn_type = htole32(STA_TYPE_AP | NETWORK_INFRA);
		break;
#ifndef IEEE80211_STA_ONLY
	case IEEE80211_M_IBSS:
	case IEEE80211_M_AHDEMO:
		basic->conn_type = htole32(STA_TYPE_ADHOC | NETWORK_IBSS);
		break;
#endif
	case IEEE80211_M_MONITOR:
		panic("mt7921_mcu_sta_basic_tlv unexpected operation mode");
	}

	basic->aid = htole16(IEEE80211_AID(ni->ni_associd));
	memcpy(basic->peer_addr, ni->ni_macaddr, IEEE80211_ADDR_LEN);
	basic->qos = (ni->ni_flags & IEEE80211_NODE_QOS) != 0;
}

/* YYavic HE: bits of an HE capability byte selected by mask, shifted down
 * (Linux u8_get_bits). */
static inline uint8_t
yy_he_bits(uint8_t v, uint8_t mask)
{
	return mask ? (uint8_t)((v & mask) >> (__builtin_ffs(mask) - 1)) : 0;
}

/* YYavic HE: per-stream intersection of two HE-MCS maps (3 = not supported,
 * otherwise the smaller of the two maxima). */
static uint16_t
yy_he_mcs_and(uint16_t a, uint16_t b)
{
	uint16_t r = 0;
	int i;

	for (i = 0; i < 8; i++) {
		uint8_t x = (a >> (2 * i)) & 3, y = (b >> (2 * i)) & 3;
		r |= (uint16_t)((x == 3 || y == 3) ? 3 : (x < y ? x : y)) << (2 * i);
	}
	return r;
}

/* YYavic HE: STA_REC_HE and STA_REC_HE_V2 for the AP, as Linux
 * mt76_connac_mcu_sta_he_tlv() / _v2() build them from the peer's HE
 * capabilities (80 MHz). */
static void
mwx_yy_sta_he_tlv(struct mbuf *m, uint16_t *tlvnum, struct ieee80211com *ic,
    struct ieee80211_node *ni)
{
	const struct ieee80211_he_cap_elem *e = &ni->ni_he_cap_elem;
	struct sta_rec_he *he;
	struct sta_rec_he_v2 *he2;
	uint16_t mcs80;
	uint32_t cap = 0;

	mcs80 = yy_he_mcs_and(le16toh(ni->ni_he_mcs_nss_supp.rx_mcs_80),
	    le16toh(ic->ic_he_mcs_nss_supp.tx_mcs_80));
	/* 0.6.7: 160 MHz BSS -> BW160 map too (Linux: sta bandwidth 160) */
	uint16_t mcs160 = ni->ni_chw == IEEE80211_CHAN_WIDTH_160 ?
	    yy_he_mcs_and(le16toh(ni->ni_he_mcs_nss_supp.rx_mcs_160),
	    le16toh(ic->ic_he_mcs_nss_supp.tx_mcs_160)) : 0;

	if (e->mac_cap_info[0] & IEEE80211_HE_MAC_CAP0_HTC_HE)
		cap |= STA_REC_HE_CAP_HTC;
	if (e->mac_cap_info[2] & IEEE80211_HE_MAC_CAP2_BSR)
		cap |= STA_REC_HE_CAP_BSR;
	if (e->mac_cap_info[3] & IEEE80211_HE_MAC_CAP3_OMI_CONTROL)
		cap |= STA_REC_HE_CAP_OM;
	if (e->mac_cap_info[4] & IEEE80211_HE_MAC_CAP4_AMDSU_IN_AMPDU)
		cap |= STA_REC_HE_CAP_AMSDU_IN_AMPDU;
	if (e->mac_cap_info[4] & IEEE80211_HE_MAC_CAP4_BQR)
		cap |= STA_REC_HE_CAP_BQR;
	if (e->phy_cap_info[0] &
	    (IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_RU_MAPPING_IN_2G |
	     IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_RU_MAPPING_IN_5G))
		cap |= STA_REC_HE_CAP_BW20_RU242_SUPPORT;
	if (e->phy_cap_info[1] & IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD)
		cap |= STA_REC_HE_CAP_LDPC;
	if (e->phy_cap_info[1] & IEEE80211_HE_PHY_CAP1_HE_LTF_AND_GI_FOR_HE_PPDUS_0_8US)
		cap |= STA_REC_HE_CAP_SU_PPDU_1LTF_8US_GI;
	if (e->phy_cap_info[2] & IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US)
		cap |= STA_REC_HE_CAP_NDP_4LTF_3DOT2MS_GI;
	if (e->phy_cap_info[2] & IEEE80211_HE_PHY_CAP2_STBC_TX_UNDER_80MHZ)
		cap |= STA_REC_HE_CAP_LE_EQ_80M_TX_STBC;
	if (e->phy_cap_info[2] & IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ)
		cap |= STA_REC_HE_CAP_LE_EQ_80M_RX_STBC;
	if (e->phy_cap_info[6] & IEEE80211_HE_PHY_CAP6_PARTIAL_BW_EXT_RANGE)
		cap |= STA_REC_HE_CAP_PARTIAL_BW_EXT_RANGE;
	if (e->phy_cap_info[7] & IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI)
		cap |= STA_REC_HE_CAP_SU_MU_PPDU_4LTF_8US_GI;
	if (e->phy_cap_info[7] & IEEE80211_HE_PHY_CAP7_STBC_TX_ABOVE_80MHZ)
		cap |= STA_REC_HE_CAP_GT_80M_TX_STBC;
	if (e->phy_cap_info[7] & IEEE80211_HE_PHY_CAP7_STBC_RX_ABOVE_80MHZ)
		cap |= STA_REC_HE_CAP_GT_80M_RX_STBC;
	if (e->phy_cap_info[8] & IEEE80211_HE_PHY_CAP8_HE_ER_SU_PPDU_4XLTF_AND_08_US_GI)
		cap |= STA_REC_HE_CAP_ER_SU_PPDU_4LTF_8US_GI;
	if (e->phy_cap_info[8] & IEEE80211_HE_PHY_CAP8_HE_ER_SU_1XLTF_AND_08_US_GI)
		cap |= STA_REC_HE_CAP_ER_SU_PPDU_1LTF_8US_GI;
	if (e->phy_cap_info[9] & IEEE80211_HE_PHY_CAP9_NON_TRIGGERED_CQI_FEEDBACK)
		cap |= STA_REC_HE_CAP_TRIG_CQI_FK;
	if (e->phy_cap_info[9] & IEEE80211_HE_PHY_CAP9_TX_1024_QAM_LESS_THAN_242_TONE_RU)
		cap |= STA_REC_HE_CAP_TX_1024QAM_UNDER_RU242;
	if (e->phy_cap_info[9] & IEEE80211_HE_PHY_CAP9_RX_1024_QAM_LESS_THAN_242_TONE_RU)
		cap |= STA_REC_HE_CAP_RX_1024QAM_UNDER_RU242;

	he = (struct sta_rec_he *)mwx_append_tlv(m, tlvnum, STA_REC_HE, sizeof(*he));
	he->he_cap = htole32(cap);
	he->max_nss_mcs[CMD_HE_MCS_BW80] = htole16(mcs80);
	if (mcs160)
		he->max_nss_mcs[CMD_HE_MCS_BW160] = htole16(mcs160);
	he->t_frame_dur = yy_he_bits(e->mac_cap_info[1], IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_MASK);
	he->max_ampdu_exp = yy_he_bits(e->mac_cap_info[3], IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_MASK);
	he->bw_set = yy_he_bits(e->phy_cap_info[0], IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_MASK);
	he->device_class = yy_he_bits(e->phy_cap_info[1], IEEE80211_HE_PHY_CAP1_DEVICE_CLASS_A);
	he->punc_pream_rx = yy_he_bits(e->phy_cap_info[1], IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_MASK);
	he->dcm_tx_mode = yy_he_bits(e->phy_cap_info[3], IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_MASK);
	he->dcm_tx_max_nss = yy_he_bits(e->phy_cap_info[3], IEEE80211_HE_PHY_CAP3_DCM_MAX_TX_NSS_2);
	he->dcm_rx_mode = yy_he_bits(e->phy_cap_info[3], IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_MASK);
	he->dcm_rx_max_nss = yy_he_bits(e->phy_cap_info[3], IEEE80211_HE_PHY_CAP3_DCM_MAX_RX_NSS_2);
	he->dcm_max_ru = yy_he_bits(e->phy_cap_info[8], IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_MASK);
	he->pkt_ext = 2;

	he2 = (struct sta_rec_he_v2 *)mwx_append_tlv(m, tlvnum, STA_REC_HE_V2, sizeof(*he2));
	memcpy(he2->he_phy_cap, e->phy_cap_info, sizeof(he2->he_phy_cap));
	memcpy(he2->he_mac_cap, e->mac_cap_info, sizeof(he2->he_mac_cap));
	he2->max_nss_mcs[CMD_HE_MCS_BW80] = htole16(mcs80);
	if (mcs160)
		he2->max_nss_mcs[CMD_HE_MCS_BW160] = htole16(mcs160);
	he2->pkt_ext = IEEE80211_HE_PHY_CAP9_NOMIMAL_PKT_PADDING_16US;

	smtw_trace("mwx: sta_he cap=0x%06x mcs80=0x%04x mcs160=0x%04x (ap 0x%04x) bw_set=0x%x tfd=%u ampdu_exp=%u\n",
	    cap, mcs80, mcs160, le16toh(ni->ni_he_mcs_nss_supp.rx_mcs_80), he->bw_set,
	    he->t_frame_dur, he->max_ampdu_exp);
}

void
mt7921_mcu_add_sta_tlv(struct mbuf *m, uint16_t *tlvnum, struct mwx_softc *sc,
    struct ieee80211_node *ni, int add, int isnew)
{
	//struct ieee80211com *ic = &sc->sc_ic;
	struct sta_rec_ra_info *ra_info;
	struct sta_rec_state *state;
	struct sta_rec_phy *phy;
	uint16_t supp_rates;

#ifdef NOTYET
	/* sta rec ht */
	if (sta->deflink.ht_cap.ht_supported) {
		struct sta_rec_ht *ht;

		ht = mwx_append_tlv(m, tlvnum, STA_REC_HT, sizeof(*ht));
		ht->ht_cap = htole16(sta->deflink.ht_cap.cap);
	}

	/* sta rec vht */
	if (sta->deflink.vht_cap.vht_supported) {
		struct sta_rec_vht *vht;

		vht = mwx_append_tlv(m, tlvnum, STA_REC_VHT,
		    sizeof(*vht));
		vht->vht_cap = htole32(sta->deflink.vht_cap.cap);
		vht->vht_rx_mcs_map = sta->deflink.vht_cap.vht_mcs.rx_mcs_map;
		vht->vht_tx_mcs_map = sta->deflink.vht_cap.vht_mcs.tx_mcs_map;
	}

	/* sta rec uapsd */
	/* from function:
		if (vif->type != NL80211_IFTYPE_AP || !sta->wme)
			return;
	*/
	mt7921_mcu_sta_uapsd(m, tlvnum, vif, ni);
#endif

#ifdef NOTYET
	if (sta->deflink.ht_cap.ht_supported || sta->deflink.he_cap.has_he)
		mt76_connac_mcu_sta_amsdu_tlv(skb, sta, vif);

	/* sta rec he */
	if (sta->deflink.he_cap.has_he) {
		mt76_connac_mcu_sta_he_tlv(skb, sta);
		if (band == NL80211_BAND_6GHZ &&
		    sta_state == MT76_STA_INFO_STATE_ASSOC) {
			struct sta_rec_he_6g_capa *he_6g_capa;

			he_6g_capa = mwx_append_tlv(m, tlvnum,
			    STA_REC_HE_6G, sizeof(*he_6g_capa));
			he_6g_capa->capa = sta->deflink.he_6ghz_capa.capa;
		}
	}
#endif

	/* R9T trace27: sta rec ht */
	if (ni != NULL && (ni->ni_flags & IEEE80211_NODE_HT)) {
		struct sta_rec_ht *ht;

		ht = (struct sta_rec_ht *)mwx_append_tlv(m, tlvnum, STA_REC_HT,
		    sizeof(*ht));
		ht->ht_cap = htole16(ni->ni_htcaps);
	}

	/* R9T trace33: sta rec vht */
	if (ni != NULL && (ni->ni_flags & IEEE80211_NODE_VHT)) {
		struct sta_rec_vht *vht;

		vht = (struct sta_rec_vht *)mwx_append_tlv(m, tlvnum, STA_REC_VHT,
		    sizeof(*vht));
		vht->vht_cap = htole32(ni->ni_vhtcaps);
		vht->vht_rx_mcs_map = htole16(ni->ni_vht_mcsinfo.rx_mcs_map);
		vht->vht_tx_mcs_map = htole16(ni->ni_vht_mcsinfo.tx_mcs_map);
	}

	/* YYavic HE: sta rec he / he_v2 */
	if (ni != NULL && (ni->ni_flags & IEEE80211_NODE_HE))
		mwx_yy_sta_he_tlv(m, tlvnum, &sc->sc_ic, ni);

	phy = (struct sta_rec_phy *)mwx_append_tlv(m, tlvnum, STA_REC_PHY,
	    sizeof(*phy));
	/* XXX basic_rates: bitmap of basic rates, each bit stands for an
	 *      index into the rate table configured by the driver in
	 *      the current band.
	 */
	phy->basic_rate = htole16(0x0150); /* XXX */
	phy->phy_type = mt7921_get_phy_mode_v2(sc, ni);
	if (ni != NULL && (ni->ni_flags & IEEE80211_NODE_HT))
		phy->ampdu = ni->ni_ampdu_param & 0x1f;	/* factor[1:0] density[4:2] */
#ifdef NOTYET
	phy->ampdu = FIELD_PREP(IEEE80211_HT_AMPDU_PARM_FACTOR,
	    sta->deflink.ht_cap.ampdu_factor) |
	    FIELD_PREP(IEEE80211_HT_AMPDU_PARM_DENSITY,
	    sta->deflink.ht_cap.ampdu_density);
#endif
	// XXX phy->rcpi = rssi_to_rcpi(-ewma_rssi_read(&sc->sc_vif.rssi));
	phy->rcpi = 0xdc;	/* XXX STOLEN FROM LINUX DUMP */

#ifdef HACK
	supp_rates = sta->deflink.supp_rates[band];
	if (band == NL80211_BAND_2GHZ)
		supp_rates = FIELD_PREP(RA_LEGACY_OFDM, supp_rates >> 4) |
		FIELD_PREP(RA_LEGACY_CCK, supp_rates & 0xf);
	else
		supp_rates = FIELD_PREP(RA_LEGACY_OFDM, supp_rates);
#else
	supp_rates = RA_LEGACY_OFDM;
#endif

	ra_info = (struct sta_rec_ra_info *)mwx_append_tlv(m, tlvnum,
	    STA_REC_RA, sizeof(*ra_info));
	ra_info->legacy = htole16(supp_rates);
	if (ni != NULL && (ni->ni_flags & IEEE80211_NODE_HT)) {
		memcpy(ra_info->rx_mcs_bitmask, ni->ni_rxmcs, HT_MCS_MASK_NUM);
		/* kendi akis sayimizla sinirla */
		if (sc->sc_capa.num_streams < 2)
			memset(ra_info->rx_mcs_bitmask + 1, 0, HT_MCS_MASK_NUM - 1);
		else
			memset(ra_info->rx_mcs_bitmask + 2, 0, HT_MCS_MASK_NUM - 2);
	}
	{
		static uint32_t n;
		if (ni != NULL && n++ < 6)
			smtw_trace("mwx: sta_tlv ht=%d htcaps=0x%04x ampdu=0x%02x rxmcs=%02x%02x phy=0x%02x\n",
			    (ni->ni_flags & IEEE80211_NODE_HT) != 0, ni->ni_htcaps, ni->ni_ampdu_param,
			    ni->ni_rxmcs[0], ni->ni_rxmcs[1], phy->phy_type);
	}
#ifdef NOTYET
	if (sta->deflink.ht_cap.ht_supported)
		memcpy(ra_info->rx_mcs_bitmask,
			sta->deflink.ht_cap.mcs.rx_mask,
			HT_MCS_MASK_NUM);
#endif

	state = (struct sta_rec_state *)mwx_append_tlv(m, tlvnum,
	    STA_REC_STATE, sizeof(*state));
	state->state = /* XXX sta_state */ 0;
	if (ni != NULL && (ni->ni_flags & IEEE80211_NODE_VHT)) {
		/* R9T trace33: IEEE80211_STA_RX_BW_* + (rx_nss-1) << 4 */
		uint8_t bwv = ni->ni_chw == IEEE80211_CHAN_WIDTH_80 ? 2 :
		    ni->ni_chw == IEEE80211_CHAN_WIDTH_160 ? 3 :
		    ni->ni_chw == IEEE80211_CHAN_WIDTH_40 ? 1 : 0;
		state->vht_opmode = bwv | ((sc->sc_capa.num_streams > 1 ? 1 : 0) << 4);
	}
#ifdef NOTYET
	if (sta->deflink.vht_cap.vht_supported) {
		state->vht_opmode = sta->deflink.bandwidth;
		state->vht_opmode |= (sta->deflink.rx_nss - 1) <<
			IEEE80211_OPMODE_NOTIF_RX_NSS_SHIFT;
	}
#endif
}

int
mt7921_mcu_wtbl_generic_tlv(struct mbuf *m, uint16_t *tlvnum,
    struct mwx_softc *sc, struct ieee80211_node *ni)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct wtbl_generic *generic;
	struct wtbl_rx *rx;

	generic = (struct wtbl_generic *)mwx_append_tlv(m, tlvnum,
	    WTBL_GENERIC, sizeof(*generic));

	if (ni) {
		generic->partial_aid = htole16(IEEE80211_AID(ni->ni_associd));
		memcpy(generic->peer_addr, ni->ni_macaddr, IEEE80211_ADDR_LEN);
		generic->muar_idx = sc->sc_vif.omac_idx;
		generic->qos = (ni->ni_flags & IEEE80211_NODE_QOS) != 0;
	} else {
		memset(generic->peer_addr, 0xff, IEEE80211_ADDR_LEN);
		generic->muar_idx = 0xe;
	}

	rx = (struct wtbl_rx *)mwx_append_tlv(m, tlvnum, WTBL_RX,
	    sizeof(*rx));
#ifndef IEEE80211_STA_ONLY
	rx->rca1 = ni ? ic->ic_opmode != IEEE80211_M_HOSTAP : 1;
#else
	rx->rca1 = 1;
#endif
	rx->rca2 = 1;
	rx->rv = 1;

	return sizeof(*generic) + sizeof(*rx);
}

int
mt7921_mcu_wtbl_hdr_trans_tlv(struct mbuf *m, uint16_t *tlvnum,
    struct mwx_softc *sc, struct ieee80211_node *ni)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct wtbl_hdr_trans *htr;

	htr = (struct wtbl_hdr_trans *)mwx_append_tlv(m, tlvnum,
	    WTBL_HDR_TRANS, sizeof(*htr));

	/* no hdr decapsulation offload */
	htr->no_rx_trans = 1;

	if (ic->ic_opmode == IEEE80211_M_STA)
		htr->to_ds = 1;
	else
		htr->from_ds = 1;

	return sizeof(*htr);
}

int
mt7921_mcu_wtbl_ht_tlv(struct mbuf *m, uint16_t *tlvnum,
    struct mwx_softc *sc, struct ieee80211_node *ni)
{
	struct wtbl_smps *smps;
	int len = 0;

	/* R9T trace27: Linux mt76_connac_mcu_wtbl_ht_tlv (HT kismi) */
	if (ni != NULL && (ni->ni_flags & IEEE80211_NODE_HT)) {
		struct wtbl_ht *ht;

		ht = (struct wtbl_ht *)mwx_append_tlv(m, tlvnum, WTBL_HT,
		    sizeof(*ht));
		ht->ht = 1;
		ht->ldpc = (ni->ni_htcaps & IEEE80211_HTCAP_LDPC) != 0;
		ht->af = ni->ni_ampdu_param & 0x03;
		ht->mm = (ni->ni_ampdu_param >> 2) & 0x07;
		len += sizeof(*ht);
	}
	/* R9T trace33: WTBL_VHT */
	if (ni != NULL && (ni->ni_flags & IEEE80211_NODE_VHT)) {
		struct wtbl_vht *vht;

		vht = (struct wtbl_vht *)mwx_append_tlv(m, tlvnum, WTBL_VHT,
		    sizeof(*vht));
		vht->vht = 1;
		vht->ldpc = (ni->ni_vhtcaps & IEEE80211_VHTCAP_RXLDPC) != 0;
		len += sizeof(*vht);
	}

	smps = (struct wtbl_smps *)mwx_append_tlv(m, tlvnum, WTBL_SMPS,
	    sizeof(*smps));
	/* spatial multiplexing power save mode, off for now */
	//smps->smps = (sta->deflink.smps_mode == IEEE80211_SMPS_DYNAMIC);

	return len + sizeof(*smps);
}

int
mt7921_mac_sta_update(struct mwx_softc *sc, struct ieee80211_node *ni,
    int add, int isnew)
{
	struct mwx_node *mw = (struct mwx_node *)ni;
	struct mwx_vif *mvif = &sc->sc_vif;
	struct sta_rec_wtbl *wtbl;
	struct mbuf *m = NULL;
	uint16_t tlvnum = 0, wnum = 0;
	int wlen = 0;

	m = mwx_alloc_sta_req_tlv(sizeof(struct sta_req_hdr));
	if (m == NULL)
		return ENOBUFS;

	if (ni != NULL)
		mt7921_mcu_add_basic_tlv(m, &tlvnum, sc, ni, add, isnew);

	if (ni != NULL && add)
		mt7921_mcu_add_sta_tlv(m, &tlvnum, sc, ni, add, isnew);

	wtbl = (struct sta_rec_wtbl *)mwx_append_tlv(m, &tlvnum,
	    STA_REC_WTBL, sizeof(*wtbl));
	wtbl->wlan_idx_lo = mw ? mw->wcid & 0xff : 0;
	wtbl->wlan_idx_hi = mw ? mw->wcid >> 8 : 0;
	wtbl->operation = WTBL_RESET_AND_SET;

	if (add) {
		wlen += mt7921_mcu_wtbl_generic_tlv(m, &wnum, sc, ni);
		wlen += mt7921_mcu_wtbl_hdr_trans_tlv(m, &wnum, sc, ni);

		if (ni)
			wlen += mt7921_mcu_wtbl_ht_tlv(m, &wnum, sc, ni);
	}

	wtbl->tlv_num = htole16(wnum);
	wtbl->len = htole16(le16toh(wtbl->len) + wlen);

	m = mwx_fill_sta_req_hdr(m, mvif, ni ? mvif->omac_idx : 0,
	    mw ? mw->wcid : 0, tlvnum);
	if (m == NULL)
		return ENOBUFS;

	return mwx_mcu_send_mbuf_wait(sc, MCU_UNI_CMD_STA_REC_UPDATE, m);
}

static int
mt7921_key_to_cipher_id(struct ieee80211_key *k)
{
	switch (k->k_cipher) {
	case IEEE80211_CIPHER_CCMP:
		 return MCU_CIPHER_AES_CCMP;
	case IEEE80211_CIPHER_TKIP:
		return MCU_CIPHER_TKIP;
	case IEEE80211_CIPHER_WEP40:
		return MCU_CIPHER_WEP40;
	case IEEE80211_CIPHER_WEP104:
		 return MCU_CIPHER_WEP104;
	case IEEE80211_CIPHER_BIP:
		return MCU_CIPHER_BIP_CMAC_128;
	default:
		return MCU_CIPHER_NONE;
	}
}

void
mt7921_mcu_add_key_tlv(struct mbuf *m, uint16_t *tlvnum,
    struct ieee80211_key *k, int add)
{
	struct sta_rec_sec *sec;
	struct sta_rec_sec_key *key;

	sec = (struct sta_rec_sec *)mwx_append_tlv(m, tlvnum, STA_REC_KEY,
	    sizeof(*sec));

	if (add) {
		sec->n_cipher = 1;
		sec->add = 0;	/* connac2 has inverted logic */
		sec->len = htole16(sizeof(*sec) + sizeof(*key));

		key = (struct sta_rec_sec_key *)mwx_append_len(m, sizeof(*key));
		key->cipher_id = mt7921_key_to_cipher_id(k);
		key->cipher_len = sizeof(*key);
		key->key_id = k->k_id;
		key->key_len = k->k_len;

		KASSERT(k->k_len <= sizeof(key->key));
		memcpy(key->key, k->k_key, k->k_len);

		if (key->cipher_id == MCU_CIPHER_TKIP &&
		    k->k_len == sizeof(key->key)) {
			/* Rx/Tx MIC keys are swapped. */
			memcpy(key->key + 16, k->k_key + 24, 8);
			memcpy(key->key + 24, k->k_key + 16, 8);
		}
	} else {
		sec->add = 1;
		sec->n_cipher = 0;
	}
}

int
mt7921_mcu_sta_key_update(struct mwx_softc *sc, struct ieee80211_node *ni,
    struct ieee80211_key *k)
{
	struct mwx_vif *mvif = &sc->sc_vif;
	struct mwx_node *mn = (struct mwx_node *)ni;
	struct mbuf *m;
	int want_keymask = (MWX_NODE_FLAG_HAVE_PAIRWISE_KEY |
	    MWX_NODE_FLAG_HAVE_GROUP_KEY);
	int rv;
	uint16_t tlvnum = 0, wcid = 0;
	uint8_t muar_idx = 0;

	if (ni == NULL) {
		ni = sc->sc_ic.ic_bss;
		mn = (struct mwx_node *)ni;
	}
	if (ni == NULL)
		return ENXIO;

	if ((k->k_flags & IEEE80211_KEY_GROUP) != 0) {
		wcid = mvif->vif_mn.wcid;
		muar_idx = 0x0e;
	} else {
		wcid = mn->wcid;
	}

	m = mwx_alloc_sta_req_tlv(sizeof(struct sta_req_hdr));
	if (m == NULL)
		return ENOBUFS;

	DPRINTF("%s: %s: ni %p, k_id %d, k_flags %x k_cipher %d wcid %d\n",
	    DEVNAME(sc), "add key", ni, k->k_id, k->k_flags, k->k_cipher, wcid);
	MWX_DEV_LOG(sc,
	    "key add request id=%d flags=0x%x cipher=%d len=%d wcid=%u muar=%u ni=%p\n",
	    k->k_id, k->k_flags, k->k_cipher, k->k_len, wcid, muar_idx, ni);

	mt7921_mcu_add_key_tlv(m, &tlvnum, k, 1);
	m = mwx_fill_sta_req_hdr(m, mvif, muar_idx, wcid, tlvnum);
	if (m == NULL)
		return ENOBUFS;

	rv = mwx_mcu_send_mbuf_wait(sc, MCU_UNI_CMD_STA_REC_UPDATE, m);
	if (rv != 0)
		return rv;
	MWX_DEV_LOG(sc,
	    "key add done id=%d flags=0x%x cipher=%d wcid=%u node_flags_before=0x%x\n",
	    k->k_id, k->k_flags, k->k_cipher, wcid, mn->flags);

	/* Keep track of added keys to trigger link up when all are done. */
	if ((k->k_flags & IEEE80211_KEY_GROUP) != 0)
		mn->flags |= MWX_NODE_FLAG_HAVE_GROUP_KEY;
	else
		mn->flags |= MWX_NODE_FLAG_HAVE_PAIRWISE_KEY;

	if ((mn->flags & want_keymask) == want_keymask) {
		DPRINTF("marking port %s valid\n", ether_sprintf(ni->ni_macaddr));
		MWX_DEV_LOG(sc,
		    "key add complete port valid mac=%s node_flags=0x%x\n",
		    ether_sprintf(ni->ni_macaddr), mn->flags);
		if (ni->ni_flags & IEEE80211_NODE_MFP)
			ni->ni_flags |= IEEE80211_NODE_TXMGMTPROT;
		ni->ni_port_valid = 1;
		ieee80211_set_link_state(&sc->sc_ic, LINK_STATE_UP);
	}

	return 0;
}

void
mt7921_mcu_sta_key_delete(struct mwx_softc *sc, struct ieee80211_node *ni,
    struct ieee80211_key *k)
{
	struct mwx_vif *mvif = &sc->sc_vif;
	struct mwx_node *mn = (struct mwx_node *)ni;
	struct mbuf *m;
	uint16_t tlvnum = 0, wcid = 0;
	uint8_t muar_idx = 0;

	if (ni == NULL) {
		ni = sc->sc_ic.ic_bss;
		mn = (struct mwx_node *)ni;
	}
	if (ni == NULL)
		return;

	if ((k->k_flags & IEEE80211_KEY_GROUP) != 0) {
		wcid = mvif->vif_mn.wcid;
		muar_idx = 0x0e;
		mn->flags &= ~MWX_NODE_FLAG_HAVE_GROUP_KEY;
	} else {
		wcid = mn->wcid;
		mn->flags &= ~MWX_NODE_FLAG_HAVE_PAIRWISE_KEY;
	}

	m = mwx_alloc_sta_req_tlv(sizeof(struct sta_req_hdr));
	if (m == NULL)
		return;

	DPRINTF("%s: %s: ni %p, k_id %d, k_flags %x k_cipher %d wcid %d\n",
	    DEVNAME(sc), "delete key", ni, k->k_id, k->k_flags, k->k_cipher,
	    wcid);

	mt7921_mcu_add_key_tlv(m, &tlvnum, k, 0);
	m = mwx_fill_sta_req_hdr(m, mvif, muar_idx, wcid, tlvnum);
	if (m == NULL)
		return;

	mwx_mcu_send_mbuf(sc, MCU_UNI_CMD_STA_REC_UPDATE, m, NULL);
}

#undef free

bool
MtkMwx::mwx_match(IOPCIDevice *device)
{
	if (device == NULL) {
		MWX_LOG("probe skipped: null PCI device\n");
		return false;
	}
	uint16_t vendor = device->configRead16(kIOPCIConfigVendorID);
	uint16_t product = device->configRead16(kIOPCIConfigDeviceID);
	bool matched = pci_matchbyid(vendor, product, mwx_devices,
	    nitems(mwx_devices)) != 0;
	MWX_LOG("probe vendor=0x%04x product=0x%04x matched=%d\n",
	    vendor, product, matched);
	return matched;
}

bool
MtkMwx::attach(IOPCIDevice *device)
{
	MWX_LOG("HAL attach begin device=%p\n", (void *)device);
	if (com == NULL)
		com = (struct mwx_softc *)malloc(sizeof(*com), M_DEVBUF, M_WAITOK | M_ZERO);
	if (pci == NULL)
		pci = (struct pci_attach_args *)malloc(sizeof(*pci), M_DEVBUF, M_WAITOK | M_ZERO);
	if (com == NULL || pci == NULL) {
		MWX_LOG("HAL attach allocation failed com=%p pci=%p\n",
		    (void *)com, (void *)pci);
		releaseAll();
		return false;
	}
	MWX_LOG("HAL attach allocated com=%p pci=%p\n",
	    (void *)com, (void *)pci);

	if (!taskq_init()) {
		releaseAll();
		return false;
	}
	taskQueueOwned = true;
	gMwxHal = this;
	pci->pa_tag = device;
	pci->workloop = getMainWorkLoop();
	pci->pa_id = ((pcireg_t)device->configRead16(kIOPCIConfigDeviceID) << 16) |
	    device->configRead16(kIOPCIConfigVendorID);
	strlcpy(com->sc_dev.dv_xname, "mwx0", sizeof(com->sc_dev.dv_xname));
	MWX_LOG("HAL attach pci_id=0x%08x workloop=%p\n",
	    pci->pa_id, (void *)pci->workloop);
	if (!mwx_attach(NULL, (struct device *)com, pci)) {
		MWX_LOG("HAL attach failed inside mwx_attach\n");
		detach(device);
		return false;
	}
	MWX_LOG("HAL attach complete\n");
	return true;
}

void
MtkMwx::detach(IOPCIDevice *device)
{
    MWX_LOG("HAL detach begin device=%p com=%p\n", (void *)device, (void *)com);
    if (com == NULL) {
        if (taskQueueOwned) { taskq_destroy(systq); taskQueueOwned = false; }
        releaseAll();
        return;
    }
    struct ifnet *ifp = &com->sc_ic.ic_if;
    com->sc_detaching = true;
    timeout_del(&com->sc_reset_to);
    /* Keep interrupts alive while in-flight MCU requests finish. */
    taskq_quiesce(com->sc_nswq);
    if (taskQueueOwned) taskq_quiesce(systq);
    if (ifp->if_flags & IFF_RUNNING) mwx_stop(ifp);
    timeout_free(&com->sc_reset_to);
    /* No interrupt callback may access buffers during their destruction. */
    if (com->sc_ih) {
        pci_intr_disestablish(pci->pa_pc, com->sc_ih);
        com->sc_ih = NULL;
    }
    if (com->sc_memh) {
        mwx_write(com, MT_WFDMA0_HOST_INT_ENA, 0);
        mwx_dma_disable(com, 1);
    }
    if (com->sc_net_attached) {
        ieee80211_ifdetach(ifp);
        com->sc_net_attached = false;
    }
    taskq_destroy(com->sc_nswq);
    com->sc_nswq = NULL;
    if (taskQueueOwned) { taskq_destroy(systq); taskQueueOwned = false; }
    mwx_txwi_free(com);
    mwx_dma_free(com);
    if (com->sc_memh) {
        bus_space_unmap(com->sc_st, com->sc_memh, com->sc_mems);
        com->sc_memh = 0;
    }
    releaseAll();
    MWX_LOG("HAL detach complete\n");
}

IOReturn
MtkMwx::enable(IONetworkInterface *interface)
{
	MWX_LOG("HAL enable begin interface=%p com=%p\n",
	    (void *)interface, (void *)com);
	if (com == NULL) {
		MWX_LOG("HAL enable failed: not attached\n");
		return kIOReturnNotAttached;
	}
	struct ifnet *ifp = &com->sc_ic.ic_if;
	if (ifp->if_flags & IFF_UP) {
		MWX_DEV_LOG(com, "HAL enable skipped: already up flags=0x%x\n",
		    ifp->if_flags);
		return kIOReturnSuccess;
	}
	ifp->if_flags |= IFF_UP;
	int rv = mwx_init(ifp);
	MWX_DEV_LOG(com, "HAL enable done mwx_init=%d flags=0x%x\n",
	    rv, ifp->if_flags);
	return rv == 0 ? kIOReturnSuccess : kIOReturnError;
}

IOReturn
MtkMwx::disable(IONetworkInterface *interface)
{
	MWX_LOG("HAL disable begin interface=%p com=%p\n",
	    (void *)interface, (void *)com);
	if (com == NULL)
		return kIOReturnSuccess;
	struct ifnet *ifp = &com->sc_ic.ic_if;
	if (!(ifp->if_flags & IFF_UP)) {
		MWX_DEV_LOG(com, "HAL disable skipped: already down flags=0x%x\n",
		    ifp->if_flags);
		return kIOReturnSuccess;
	}
	ifp->if_flags &= ~IFF_UP;
	if (ifp->if_flags & IFF_RUNNING)
		mwx_stop(ifp);
	MWX_DEV_LOG(com, "HAL disable complete flags=0x%x\n", ifp->if_flags);
	return kIOReturnSuccess;
}

/*
 * YYavic radio bridge: take a new own address. The firmware gets the address
 * only when the device is added (mt7921_mcu_uni_add_dev uses sc_lladdr), so
 * cycle the interface like an ifconfig lladdr change: mwx_stop removes the
 * device, mwx_init adds it again with the new sc_lladdr.
 */
IOReturn
MtkMwx::setOwnAddress(const uint8_t *mac)
{
	if (com == NULL || mac == NULL)
		return kIOReturnNotAttached;
	struct ieee80211com *ic = &com->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	if (IEEE80211_ADDR_EQ(com->sc_lladdr, mac) && IEEE80211_ADDR_EQ(ic->ic_myaddr, mac))
		return kIOReturnSuccess;
	bool up = (ifp->if_flags & IFF_UP) != 0;
	if (up)
		disable(NULL);
	IEEE80211_ADDR_COPY(com->sc_lladdr, mac);
	IEEE80211_ADDR_COPY(ic->ic_myaddr, mac);
	if_setlladdr(ifp, mac);
	IOReturn r = up ? enable(NULL) : kIOReturnSuccess;
	MWX_DEV_LOG(com, "own address now %s (cycle %d) = 0x%x\n", ether_sprintf(ic->ic_myaddr), up, r);
	return r;
}

struct ieee80211com *
MtkMwx::get80211Controller()
{
	if (com == NULL)
		com = (struct mwx_softc *)malloc(sizeof(*com), M_DEVBUF, M_WAITOK | M_ZERO);
	return com == NULL ? NULL : &com->sc_ic;
}

MtkDriverInfo *
MtkMwx::getDriverInfo()
{
	return this;
}

MtkDriverController *
MtkMwx::getDriverController()
{
	return this;
}

IOEthernetController *
MtkMwx::mwxController()
{
	return getController();
}

int
MtkMwx::mwxSleepNsec(void *ident, int priority, const char *wmesg,
    uint64_t timo)
{
	return MtkHalService::tsleep_nsec(ident, priority, wmesg, timo);
}

void
MtkMwx::mwxWakeup(void *ident)
{
	wakeupOn(ident);
}

/* YYavic: refill RX rings an earlier mwx_queue_fill could not complete. */
static void
mwx_rx_refill_check(struct mwx_softc *sc)
{
	struct mwx_queue *qs[2] = { &sc->sc_rxq, &sc->sc_rxmcuq };
	for (int k = 0; k < 2; k++) {
		struct mwx_queue *q = qs[k];
		if (q->mq_count == 0 || q->mq_data == NULL)
			continue;
		u_int last = (q->mq_count + q->mq_cons - 1) % q->mq_count;
		if (q->mq_prod == last)
			continue;
		gYYRxFillRetry++;
		(void)mwx_queue_fill(sc, q);
	}
}

static void
mwx_poll_timer_fire(OSObject *, IOTimerEventSource *t)
{
	gMwxPollFires++;
	gMwxWlThread = current_thread();
	if (gMwxPollEnable && gMwxPollSc != NULL) {
		struct mwx_softc *sc = gMwxPollSc;
		static uint32_t last_ms, samples;
		{
			uint64_t t0 = mach_absolute_time();
			(void)mwx_intr(sc);
			gPollBusyAbs += mach_absolute_time() - t0;
			gPollCalls++;
		}
		mwx_rx_refill_check(sc);	/* YYavic: RX halkasi doldurma tekrar */
		if (sc->sc_amsdu_n > 0 && ++sc->sc_amsdu_idle >= 3) {
			/* YYavic: A-MSDU left incomplete for ~2 polls: deliver it */
			struct mbuf_list ml = MBUF_LIST_INITIALIZER();
			gYYAmsduFlushEnd++;
			mwx_amsdu_flush(sc, &ml);
			if_input(&sc->sc_ic.ic_if, &ml);
		}
		mwx_tx_kick(sc);	/* R9T trace34: guvenlik agi */
		/* R9T tani: RUN durumunda 3 sn'de bir halka/MIB durumu (en fazla 14 ornek) */
		/* R9T tuzak: RUN sonrasi GLO_CFG TX/RX DMA_EN bitleri biri tarafindan siliniyor mu? */
		{
			static int armed, trapped, post, nrec;
			uint32_t glo = mwx_read(sc, MT_WFDMA0_GLO_CFG);
			if (sc->sc_ic.ic_state == IEEE80211_S_RUN && (glo & 5) == 5)
				armed = 1;
			if (armed && !trapped && nrec < 8 && (glo & 5) != 5) {
				struct mwx_queue *q = &sc->sc_txq;
				int rv;
				trapped = 1; nrec++;
				MWX_DEV_LOG(sc, "TRAP#%d glo=0x%08x st=%d int_sta=0x%08x mcucmd=0x%08x txcpu=%u txdma=%u prod=%u cons=%u dummy_cr=0x%08x lpctl=0x%08x rxq cpu=%u dma=%u\n",
				    nrec, glo, (int)sc->sc_ic.ic_state, mwx_read(sc, MT_WFDMA0_HOST_INT_STA), mwx_read(sc, MT_MCU_CMD),
				    mwx_read(sc, q->mq_regbase + MT_DMA_CPU_IDX), mwx_read(sc, q->mq_regbase + MT_DMA_DMA_IDX),
				    q->mq_prod, q->mq_cons, mwx_read(sc, MT_WFDMA_DUMMY_CR), mwx_read(sc, MT_CONN_ON_LPCTL),
				    mwx_read(sc, sc->sc_rxq.mq_regbase + MT_DMA_CPU_IDX), mwx_read(sc, sc->sc_rxq.mq_regbase + MT_DMA_DMA_IDX));
				/* Linux mt792x_wpdma_reinit_cond benzeri: tam WPDMA yeniden baslatma */
				{
					uint32_t hn = gMwxHistN, hk;
					for (hk = 0; hk < 8 && hk < hn; hk++) {
						uint32_t *e = gMwxHist[(hn - 1 - hk) & 15];
						MWX_DEV_LOG(sc, "TRAP hist[-%u] ms=%u intr=0x%08x sw=0x%08x\n", hk, e[0], e[1], e[2]);
					}
					MWX_DEV_LOG(sc, "TRAP arb=0x%08x\n", mwx_read(sc, MT_ARB_SCR(0)));
				}
				if (gMwxTrapMode == 2)
					rv = mwx_dma_reset(sc, 0);
				else {
					if (gMwxTrapMode == 1)
						mwx_set(sc, MT_WFDMA0_GLO_CFG, MT_WFDMA0_GLO_CFG_TX_DMA_EN | MT_WFDMA0_GLO_CFG_RX_DMA_EN);
					rv = 0;
				}
				MWX_DEV_LOG(sc, "TRAP#%d dma_reset rv=%d glo=0x%08x rxq cpu=%u dma=%u\n", nrec, rv,
				    mwx_read(sc, MT_WFDMA0_GLO_CFG),
				    mwx_read(sc, sc->sc_rxq.mq_regbase + MT_DMA_CPU_IDX), mwx_read(sc, sc->sc_rxq.mq_regbase + MT_DMA_DMA_IDX));
				post = 1;
			} else if (post > 0 && ++post == 100) {
				struct mwx_queue *q = &sc->sc_txq;
				MWX_DEV_LOG(sc, "TRAP+200ms glo=0x%08x txcpu=%u txdma=%u rxq cpu=%u dma=%u int_sta=0x%08x\n",
				    mwx_read(sc, MT_WFDMA0_GLO_CFG), mwx_read(sc, q->mq_regbase + MT_DMA_CPU_IDX),
				    mwx_read(sc, q->mq_regbase + MT_DMA_DMA_IDX),
				    mwx_read(sc, sc->sc_rxq.mq_regbase + MT_DMA_CPU_IDX), mwx_read(sc, sc->sc_rxq.mq_regbase + MT_DMA_DMA_IDX),
				    mwx_read(sc, MT_WFDMA0_HOST_INT_STA));
				post = 0; trapped = 0;
			}
		}
		static int seen_run;
		if (sc->sc_ic.ic_state == IEEE80211_S_RUN)
			seen_run = 1;
		if (seen_run && samples < 40) {
			clock_sec_t sec; clock_usec_t usec; clock_get_system_microtime(&sec, &usec);
			uint32_t ms = (uint32_t)(sec * 1000 + usec / 1000);
			if (ms - last_ms >= 1500) {
				last_ms = ms; samples++;
				struct mwx_queue *qs[6] = { &sc->sc_txq, &sc->sc_txmcuq, &sc->sc_txfwdlq,
				    &sc->sc_rxq, &sc->sc_rxmcuq, &sc->sc_rxfwdlq };
				MWX_DEV_LOG(sc, "RUN#%u st=%d TXD=%u/%u RXD=%u/%u RXM=%u/%u TXM=%u/%u int=%08x glo=%08x\n",
				    samples, (int)sc->sc_ic.ic_state,
				    mwx_read(sc, qs[0]->mq_regbase + MT_DMA_CPU_IDX), mwx_read(sc, qs[0]->mq_regbase + MT_DMA_DMA_IDX),
				    mwx_read(sc, qs[3]->mq_regbase + MT_DMA_CPU_IDX), mwx_read(sc, qs[3]->mq_regbase + MT_DMA_DMA_IDX),
				    mwx_read(sc, qs[4]->mq_regbase + MT_DMA_CPU_IDX), mwx_read(sc, qs[4]->mq_regbase + MT_DMA_DMA_IDX),
				    mwx_read(sc, qs[1]->mq_regbase + MT_DMA_CPU_IDX), mwx_read(sc, qs[1]->mq_regbase + MT_DMA_DMA_IDX),
				    mwx_read(sc, MT_WFDMA0_HOST_INT_STA), mwx_read(sc, MT_WFDMA0_GLO_CFG));
				MWX_DEV_LOG(sc, "RUN#%u air tx=%u rx=%u normal=%u notify=%u\n", samples,
				    mwx_read(sc, MT_MIB_SDR36(0)) & MT_MIB_SDR36_TXTIME_MASK,
				    mwx_read(sc, MT_MIB_SDR37(0)) & MT_MIB_SDR37_RXTIME_MASK,
				    (unsigned)sc->sc_rx_pkt_normal, (unsigned)sc->sc_rx_pkt_txrx_notify);
			}
		}
	}
	t->setTimeoutUS(gMwxPollUs);
}

void
MtkMwx::mwxStartPoll(struct mwx_softc *sc)
{
	IOWorkLoop *wl = getMainWorkLoop();

	if (gMwxPollTimer != NULL || wl == NULL)
		return;
	gMwxPollSc = sc;
	gMwxPollTimer = IOTimerEventSource::timerEventSource(this, mwx_poll_timer_fire);
	if (gMwxPollTimer == NULL || wl->addEventSource(gMwxPollTimer) != kIOReturnSuccess) {
		MWX_DEV_LOG(sc, "poll timer olusturulamadi\n");
		return;
	}
	gMwxPollEnable = 1;
	gMwxPollTimer->setTimeoutMS(2);
	MWX_DEV_LOG(sc, "poll timer basladi (2 ms)\n");
}

bool
MtkMwx::mwxOnWorkLoop()
{
	IOWorkLoop *wl = getMainWorkLoop();
	if (gMwxWlThread != NULL && current_thread() == gMwxWlThread)
		return true;
	return wl != NULL && wl->onThread();
}

const char *
MtkMwx::getFirmwareVersion()
{
	if (com == NULL)
		return "MT792x";
	return com->sc_hwtype == MWX_HW_MT7922 ? "MT7922" : "MT7921";
}

int16_t
MtkMwx::getBSSNoise()
{
	return 0;
}

bool
MtkMwx::is5GBandSupport()
{
	return com != NULL && com->sc_capa.has_5ghz;
}

int
MtkMwx::getTxNSS()
{
	return (com != NULL && com->sc_capa.num_streams > 0) ? com->sc_capa.num_streams : 1;
}

const char *
MtkMwx::getFirmwareName()
{
	if (com == NULL || com->sc_hwtype == MWX_HW_MT7922)
		return MT7922_FIRMWARE_WM;
	return MT7921_FIRMWARE_WM;
}

UInt32
MtkMwx::supportedFeatures()
{
	return kIONetworkFeatureMultiPages;
}

const char *
MtkMwx::getFirmwareCountryCode()
{
	static char alpha2[3] = "00";
	if (com != NULL) {
		alpha2[0] = com->sc_alpha2[0];
		alpha2[1] = com->sc_alpha2[1];
		alpha2[2] = '\0';
	}
	return alpha2;
}

uint32_t
MtkMwx::getTxQueueSize()
{
	return 256;
}

void
MtkMwx::clearScanningFlags()
{
	if (com)
		com->sc_flags &= ~(MWX_FLAG_SCANNING | MWX_FLAG_BGSCAN);
}

IOReturn
MtkMwx::setMulticastList(IOEthernetAddress *addr, int count)
{
	return kIOReturnSuccess;
}

void
MtkMwx::releaseAll()
{
	MWX_LOG("HAL releaseAll com=%p pci=%p\n", (void *)com, (void *)pci);
	if (com) {
		mwx_free_mem(com);
		com = NULL;
	}
	if (pci) {
		mwx_free_mem(pci);
		pci = NULL;
	}
	if (gMwxHal == this)
		gMwxHal = NULL;
}

void
MtkMwx::free()
	{
		MWX_LOG("HAL free\n");
		releaseAll();
		MtkHalService::free();
	}

static int mwx_send_assoc_status(struct mwx_softc *sc, uint32_t cmd, void *data, size_t len)
{
    int seq, rv;
    uint32_t status = 0;
    rv = mwx_mcu_send_msg(sc, cmd, data, len, &seq);
    if (rv) return rv;
    rv = mwx_mcu_wait_resp_int(sc, cmd, seq, &status);
    MWX_DEV_LOG(sc, "assoc_status cmd=%x transport=%d firmware=%u\n", cmd, rv, status);
    return rv ? rv : (status ? EIO : 0);
}

/* Publish the associated peer without resetting WTBL or key state. */
int mt7921_mcu_set_associated_peer(struct mwx_softc *sc)
{
    struct ieee80211_node *ni = sc->sc_ic.ic_bss;
    if (!ni || IEEE80211_AID(ni->ni_associd) == 0) return EINVAL;
    struct mbuf *m = mwx_alloc_sta_req_tlv(sizeof(struct sta_req_hdr));
    if (!m) return ENOBUFS;
    uint16_t tlvnum = 0;
    mt7921_mcu_add_basic_tlv(m, &tlvnum, sc, ni, 1, 0);
    struct sta_rec_state *state = (struct sta_rec_state *)mwx_append_tlv(m, &tlvnum, STA_REC_STATE, sizeof(*state));
    state->state = 2; /* MT76_STA_INFO_STATE_ASSOC: NONE=0, AUTH=1, ASSOC=2. */
    m = mwx_fill_sta_req_hdr(m, &sc->sc_vif, sc->sc_vif.omac_idx, ((struct mwx_node *)ni)->wcid, tlvnum);
    if (!m) return ENOBUFS;
    int seq, rv = mwx_mcu_send_mbuf(sc, MCU_UNI_CMD_STA_REC_UPDATE, m, &seq);
    uint32_t status = 0;
    if (!rv) rv = mwx_mcu_wait_resp_int(sc, MCU_UNI_CMD_STA_REC_UPDATE, seq, &status);
    MWX_DEV_LOG(sc, "assoc_peer aid=%u state=2 transport=%d firmware=%u\n", IEEE80211_AID(ni->ni_associd), rv, status);
    return rv ? rv : (status ? EIO : 0);
}

int
mt7921_mcu_set_associated_bss(struct mwx_softc *sc, int associated)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = ic->ic_bss;
	struct mwx_vif *mvif = &sc->sc_vif;
	struct {
		uint8_t bss_idx;
		uint8_t pad[3];
		struct mt76_connac_bss_basic_tlv basic;
	} __packed bss = {};
	struct {
		uint8_t bss_idx;
		uint8_t pad[3];
		struct {
			uint16_t tag, len;
			uint8_t control_channel, center_chan, center_chan2, bw;
			uint8_t tx_streams, rx_streams, short_st, ht_op_info;
			uint8_t sco, band, pad[2];
		} __packed rlm;
	} __packed channel = {};
	int rv, is_2g;

	if (ic->ic_opmode != IEEE80211_M_STA)
		return 0;
	if (ni == NULL || ni->ni_chan == NULL ||
	    ni->ni_chan == IEEE80211_CHAN_ANYC)
		return EINVAL;
	/* R9T trace33: 40/80 MHz destekli (eskiden 20 MHz disi EOPNOTSUPP) */
	is_2g = IEEE80211_IS_CHAN_2GHZ(ni->ni_chan);
	bss.bss_idx = mvif->idx;
	bss.basic.tag = htole16(UNI_BSS_INFO_BASIC);
	bss.basic.len = htole16(sizeof(bss.basic));
	bss.basic.active = 1;
	bss.basic.omac_idx = mvif->omac_idx;
	bss.basic.hw_bss_idx = mvif->omac_idx > EXT_BSSID_START ?
	    HW_BSSID_0 : mvif->omac_idx;
	bss.basic.band_idx = mvif->band_idx;
	bss.basic.conn_type = htole32(STA_TYPE_STA | NETWORK_INFRA);
	bss.basic.conn_state = associated ? 0 : 1;
	bss.basic.wmm_idx = mvif->wmm_idx;
	memcpy(bss.basic.bssid, ni->ni_bssid, ETHER_ADDR_LEN);
	bss.basic.bmc_tx_wlan_idx = htole16(mvif->vif_mn.wcid);
	bss.basic.sta_idx = htole16(mvif->vif_mn.wcid);
	bss.basic.bcn_interval = htole16(ni->ni_intval);
	bss.basic.dtim_period = ni->ni_dtimperiod;
	/* BSS phymode uses A/B/G bits; nonht_basic_phy uses PHY_TYPE bits. */
	bss.basic.phymode = is_2g ? ((1U << 1) | (1U << 2)) : (1U << 0);
	if (ni->ni_flags & IEEE80211_NODE_HT)	/* R9T trace27: PHY_MODE_GN / PHY_MODE_AN */
		bss.basic.phymode |= is_2g ? (1U << 3) : (1U << 4);
	if (!is_2g && (ni->ni_flags & IEEE80211_NODE_VHT))	/* R9T trace33: PHY_MODE_AC */
		bss.basic.phymode |= (1U << 5);
	if (ni->ni_flags & IEEE80211_NODE_HE)	/* YYavic HE: PHY_MODE_AX_24G / PHY_MODE_AX_5G */
		bss.basic.phymode |= is_2g ? (1U << 6) : (1U << 7);
	bss.basic.nonht_basic_phy = htole16(is_2g ?
	    PHY_TYPE_BIT_HR_DSSS | PHY_TYPE_BIT_ERP : PHY_TYPE_BIT_OFDM);
	rv = mwx_send_assoc_status(sc, MCU_UNI_CMD_BSS_INFO_UPDATE, &bss, sizeof(bss));
	MWX_DEV_LOG(sc, "associated BSS enable=%d channel=%u beacon=%u dtim=%u rv=%d\n",
	    associated, ieee80211_mhz2ieee(ni->ni_chan->ic_freq, 0), ni->ni_intval, ni->ni_dtimperiod, rv);
	if (rv != 0 || !associated)
		return rv;

	channel.bss_idx = mvif->idx;
	channel.rlm.tag = htole16(UNI_BSS_INFO_RLM);
	channel.rlm.len = htole16(sizeof(channel.rlm));
	channel.rlm.control_channel = ieee80211_mhz2ieee(ni->ni_chan->ic_freq, 0);
	channel.rlm.center_chan = channel.rlm.control_channel;
	channel.rlm.bw = mwx_bss_bw(sc, &channel.rlm.center_chan);	/* R9T trace33 */
	if (channel.rlm.control_channel < channel.rlm.center_chan)
		channel.rlm.sco = 1;	/* SCA */
	else if (channel.rlm.control_channel > channel.rlm.center_chan)
		channel.rlm.sco = 3;	/* SCB */
	channel.rlm.tx_streams = sc->sc_capa.num_streams;
	/* 0.6.8: as Linux mt76_connac_mcu_uni_set_chctx -- rx_streams is the
	 * chain mask, ht_op_info 4 (HT 40 allowed) for 40 MHz and wider.
	 * dbg 36 overrides both for live tests. */
	channel.rlm.rx_streams = gYYRlmRx != 0xff ? gYYRlmRx : sc->sc_capa.antenna_mask;
	channel.rlm.ht_op_info = gYYRlmHtOp != 0xff ? gYYRlmHtOp :
	    (channel.rlm.bw != CMD_CBW_20MHZ ? 4 : 0);
	channel.rlm.short_st = 1;
	channel.rlm.band = is_2g ? 0 : 1;
	rv = mwx_send_assoc_status(sc, MCU_UNI_CMD_BSS_INFO_UPDATE,
	    &channel, sizeof(channel));
	MWX_DEV_LOG(sc, "associated BSS RLM channel=%u center=%u bw=%u sco=%u band=%u ht=%d vht=%d he=%d chw=%u rv=%d\n",
	    channel.rlm.control_channel, channel.rlm.center_chan, channel.rlm.bw, channel.rlm.sco,
	    channel.rlm.band, (ni->ni_flags & IEEE80211_NODE_HT) != 0, (ni->ni_flags & IEEE80211_NODE_VHT) != 0,
	    (ni->ni_flags & IEEE80211_NODE_HE) != 0, ni->ni_chw, rv);
	if (rv != 0 || !(ni->ni_flags & IEEE80211_NODE_HE))
		return rv;

	/* YYavic HE: HE_BASIC + BSS_COLOR, as Linux mt76_connac_mcu_uni_add_bss()
	 * does for an HE BSS (defaults: PE duration 4, RTS threshold 1023). */
	{
		struct {
			uint8_t bss_idx;
			uint8_t pad[3];
			struct bss_info_uni_he he;
			struct bss_info_uni_bss_color color;
		} __packed hereq = {};
		uint32_t op = ni->ni_he_oper_params;

		hereq.bss_idx = mvif->idx;
		hereq.he.tag = htole16(UNI_BSS_INFO_HE_BASIC);
		hereq.he.len = htole16(sizeof(hereq.he));
		hereq.he.he_pe_duration = op & IEEE80211_HE_OPERATION_DFLT_PE_DURATION_MASK;
		if (hereq.he.he_pe_duration == 0)
			hereq.he.he_pe_duration = 4;
		hereq.he.he_rts_thres = htole16((op & IEEE80211_HE_OPERATION_RTS_THRESHOLD_MASK) >>
		    IEEE80211_HE_OPERATION_RTS_THRESHOLD_OFFSET);
		if (hereq.he.he_rts_thres == 0)
			hereq.he.he_rts_thres = htole16(1023);
		hereq.he.max_nss_mcs[CMD_HE_MCS_BW80] = ic->ic_he_mcs_nss_supp.tx_mcs_80;
		hereq.he.max_nss_mcs[CMD_HE_MCS_BW160] = ic->ic_he_mcs_nss_supp.tx_mcs_160;
		hereq.he.max_nss_mcs[CMD_HE_MCS_BW8080] = ic->ic_he_mcs_nss_supp.tx_mcs_80p80;
		hereq.color.tag = htole16(UNI_BSS_INFO_BSS_COLOR);
		hereq.color.len = htole16(sizeof(hereq.color));
		hereq.color.enable = (op & IEEE80211_HE_OPERATION_BSS_COLOR_DISABLED) == 0;
		hereq.color.bss_color = (op & IEEE80211_HE_OPERATION_BSS_COLOR_MASK) >>
		    IEEE80211_HE_OPERATION_BSS_COLOR_OFFSET;
		rv = mwx_send_assoc_status(sc, MCU_UNI_CMD_BSS_INFO_UPDATE, &hereq, sizeof(hereq));
		MWX_DEV_LOG(sc, "associated BSS HE oper=0x%08x pe=%u rts=%u color=%u/%u rv=%d\n",
		    op, hereq.he.he_pe_duration, le16toh(hereq.he.he_rts_thres),
		    hereq.color.enable, hereq.color.bss_color, rv);
	}
	return rv;
}


/* YYavic dbg 27-29: net80211 RX reorder helpers (ieee80211_input.c) and the
 * radio bridge hook counter (mtk80211/openbsd/sys/_mbuf.cpp). */
int ieee80211_input_ba_gap_skip(struct ieee80211_rx_ba *);
void ieee80211_input_ba_flush(struct ieee80211com *, struct ieee80211_node *, struct ieee80211_rx_ba *, struct mbuf_list *);
extern "C" volatile uint32_t yy_rx_hook_frames;
extern uint32_t yy_wf_ucast, yy_wf_mcast, yy_wf_keyid, yy_wf_last_fc;

extern "C" uint32_t
mwx_dbg(uint32_t op, uint32_t a, uint32_t b, char *out, size_t outlen)
{
	struct mwx_softc *sc = gMwxPollSc;
	uint32_t ret = 0;
	uint32_t k;

	if (sc == NULL) { snprintf(out, outlen, "sc yok"); return 0xdead; }
	switch (op) {
	case 1:	/* oku: a=adres */
		ret = mwx_read(sc, a);
		snprintf(out, outlen, "[%08x]=%08x", a, ret);
		break;
	case 2:	/* yaz: a=adres b=deger */
		mwx_write(sc, a, b);
		snprintf(out, outlen, "[%08x]<=%08x oku=%08x", a, b, mwx_read(sc, a));
		break;
	case 3:	/* mwx_dma_reset(0) */
		ret = (uint32_t)mwx_dma_reset(sc, 0);
		snprintf(out, outlen, "dma_reset rv=%u glo=%08x", ret, mwx_read(sc, MT_WFDMA0_GLO_CFG));
		break;
	case 4:	/* mwx_dma_enable */
		mwx_dma_enable(sc);
		snprintf(out, outlen, "dma_enable glo=%08x", mwx_read(sc, MT_WFDMA0_GLO_CFG));
		break;
	case 5:	/* GLO_CFG TX/RX EN */
		mwx_set(sc, MT_WFDMA0_GLO_CFG, MT_WFDMA0_GLO_CFG_TX_DMA_EN | MT_WFDMA0_GLO_CFG_RX_DMA_EN);
		snprintf(out, outlen, "glo=%08x", mwx_read(sc, MT_WFDMA0_GLO_CFG));
		break;
	case 6:	/* tuzak modu: a=0 log,1 EN set,2 dma_reset */
		gMwxTrapMode = (int)a;
		snprintf(out, outlen, "trap mode=%d", (int)gMwxTrapMode);
		break;
	case 7: {	/* halka durumu */
		struct mwx_queue *qs[6] = { &sc->sc_txq, &sc->sc_txmcuq, &sc->sc_txfwdlq,
		    &sc->sc_rxq, &sc->sc_rxmcuq, &sc->sc_rxfwdlq };
		size_t o = 0;
		for (k = 0; k < 6 && o < outlen; k++)
			o += snprintf(out + o, outlen - o, "%u/%u ", mwx_read(sc, qs[k]->mq_regbase + MT_DMA_CPU_IDX),
			    mwx_read(sc, qs[k]->mq_regbase + MT_DMA_DMA_IDX));
		break;
	}
	case 8: {	/* RX halkalarini tekmele: cpu idx = tek tek yaz */
		struct mwx_queue *qs[3] = { &sc->sc_rxq, &sc->sc_rxmcuq, &sc->sc_rxfwdlq };
		for (k = 0; k < 3; k++)
			mwx_write(sc, qs[k]->mq_regbase + MT_DMA_CPU_IDX, qs[k]->mq_count - 1);
		snprintf(out, outlen, "rx kick");
		break;
	}
	case 9: {	/* a'dan itibaren 8 ardisik kayit (4 bayt adim) */
		size_t o = 0;
		for (k = 0; k < 8 && o < outlen; k++)
			o += snprintf(out + o, outlen - o, "%08x ", mwx_read(sc, a + 4 * k));
		break;
	}
	case 10: {	/* kesme gecmisi son 4 */
		uint32_t hn = gMwxHistN; size_t o = 0;
		for (k = 0; k < 4 && k < hn && o < outlen; k++) {
			uint32_t *e = gMwxHist[(hn - 1 - k) & 15];
			o += snprintf(out + o, outlen - o, "%u:%x/%x ", e[0] % 100000, e[1], e[2]);
		}
		break;
	}
	case 11: {	/* RX istatistikleri */
		size_t o = 0;
		o += snprintf(out + o, outlen - o, "data=%u retry=%u g3=%u sgi=%u bw=%u/%u/%u mode:", gRxData, gRxRetry, gRxG3, gRxSgi, gRxBw[0], gRxBw[1], gRxBw[2]);
		for (k = 0; k < 8 && o < outlen; k++)
			if (gRxMode[k]) o += snprintf(out + o, outlen - o, " %u=%u", k, gRxMode[k]);
		if (o < outlen) o += snprintf(out + o, outlen - o, " ofdm:");
		for (k = 0; k < 16 && o < outlen; k++)
			if (gRxOfdm[k]) o += snprintf(out + o, outlen - o, " %u=%u", k, gRxOfdm[k]);
		if (o < outlen) o += snprintf(out + o, outlen - o, " mcs:");
		for (k = 0; k < 16 && o < outlen; k++)
			if (gRxHtMcs[k]) o += snprintf(out + o, outlen - o, " %u=%u", k, gRxHtMcs[k]);
		break;
	}
	case 12:	/* RX istatistiklerini sifirla */
		memset(gRxOfdm, 0, sizeof(gRxOfdm)); memset(gRxMode, 0, sizeof(gRxMode)); memset(gRxHtMcs, 0, sizeof(gRxHtMcs)); memset(gRxBw, 0, sizeof(gRxBw));
		gRxSgi = gRxData = gRxRetry = gRxG3 = 0;
		snprintf(out, outlen, "rx stats sifirlandi");
		break;
	case 14:	/* ikinci akis MCS maskesi (a=0 -> tek akis) */
		sc->sc_ic.ic_sup_mcs[1] = (uint8_t)a;
		snprintf(out, outlen, "sup_mcs=%02x%02x", sc->sc_ic.ic_sup_mcs[0], sc->sc_ic.ic_sup_mcs[1]);
		break;
	case 15:	/* ic_htcaps */
		sc->sc_ic.ic_htcaps = (uint16_t)a;
		snprintf(out, outlen, "htcaps=0x%04x", sc->sc_ic.ic_htcaps);
		break;
	case 16: {	/* durum */
		struct ieee80211_node *bni = sc->sc_ic.ic_bss;
		snprintf(out, outlen, "streams=%u mask=0x%x ic_htcaps=0x%04x sup=%02x%02x flags=0x%x | ni_flags=0x%x ni_htcaps=0x%04x ampdu=0x%02x rxmcs=%02x%02x htop0=0x%02x",
		    sc->sc_capa.num_streams, sc->sc_capa.antenna_mask, sc->sc_ic.ic_htcaps,
		    sc->sc_ic.ic_sup_mcs[0], sc->sc_ic.ic_sup_mcs[1], sc->sc_ic.ic_flags,
		    bni ? bni->ni_flags : 0, bni ? bni->ni_htcaps : 0, bni ? bni->ni_ampdu_param : 0,
		    bni ? bni->ni_rxmcs[0] : 0, bni ? bni->ni_rxmcs[1] : 0, bni ? bni->ni_htop0 : 0);
		break;
	}
	case 17:	/* yeniden iliskilendir (kayitli PSK ile) */
		ieee80211_new_state(&sc->sc_ic, IEEE80211_S_SCAN, -1);
		snprintf(out, outlen, "yeniden tarama/iliskilendirme baslatildi");
		break;
	case 18:	/* HT ac/kapat */
		if (a)
			sc->sc_ic.ic_flags |= IEEE80211_F_HTON;
		else
			sc->sc_ic.ic_flags &= ~IEEE80211_F_HTON;
		snprintf(out, outlen, "HTON=%d", (sc->sc_ic.ic_flags & IEEE80211_F_HTON) != 0);
		break;
	case 23: {	/* TX A-MPDU ac/kapat (yeni oturumlar); durum */
		struct ieee80211_node *bni = sc->sc_ic.ic_bss;
		size_t o = 0;
		if (b == 1) {
			if (a) sc->sc_ic.ic_caps |= IEEE80211_C_TX_AMPDU;
			else sc->sc_ic.ic_caps &= ~IEEE80211_C_TX_AMPDU;
		}
		o += snprintf(out + o, outlen - o, "txampdu=%d tx_ba:", (sc->sc_ic.ic_caps & IEEE80211_C_TX_AMPDU) != 0);
		for (k = 0; bni != NULL && k < 8 && o < outlen; k++)
			o += snprintf(out + o, outlen - o, " %u=%d", k, bni->ni_tx_ba[k].ba_state);
		break;
	}
	case 24:	/* ic_vhtcaps ayarla (a), b=1 ise ic_htcaps |= LDPC */
		sc->sc_ic.ic_vhtcaps = a;
		if (b == 1) sc->sc_ic.ic_htcaps |= IEEE80211_HTCAP_LDPC;
		if (b == 2) sc->sc_ic.ic_htcaps &= ~IEEE80211_HTCAP_LDPC;
		snprintf(out, outlen, "vhtcaps=0x%08x htcaps=0x%04x", sc->sc_ic.ic_vhtcaps, sc->sc_ic.ic_htcaps);
		break;
	case 25: {	/* workloop doluluk + VHT RX dagilimi (okuyunca sifirlanir) */
		uint64_t now = mach_absolute_time(), win, ns_busy, ns_win;
		size_t o = 0;
		uint32_t n, m2;
		if (gPollWinAbs0 == 0) gPollWinAbs0 = now;
		win = now - gPollWinAbs0;
		absolutetime_to_nanoseconds(gPollBusyAbs, &ns_busy);
		absolutetime_to_nanoseconds(win, &ns_win);
		o += snprintf(out + o, outlen - o, "busy=%u%% polls=%u win=%ums |", ns_win ? (uint32_t)(ns_busy * 100 / ns_win) : 0,
		    gPollCalls, (uint32_t)(ns_win / 1000000));
		for (n = 0; n < 2 && o < outlen; n++)
			for (m2 = 0; m2 < 10 && o < outlen; m2++)
				if (gRxVht[n][m2]) o += snprintf(out + o, outlen - o, " n%um%u=%u", n + 1, m2, gRxVht[n][m2]);
		gPollBusyAbs = 0; gPollCalls = 0; gPollWinAbs0 = now;
		memset(gRxVht, 0, sizeof(gRxVht));
		break;
	}
	case 27: {	/* YYavic: RX dusurme sayaclari (net80211) */
		struct ieee80211_stats *st = &sc->sc_ic.ic_stats;
		snprintf(out, outlen, "dup=%u wbss=%u wdir=%u nassoc=%u unenc=%u wep=%u decap=%u crc=%u unauth=%u ccmp_rep=%u ccmp_err=%u below=%u above=%u slide=%u jump=%u nobuf=%u lost=%u gapto=%u bato=%u",
		    st->is_rx_dup, st->is_rx_wrongbss, st->is_rx_wrongdir, st->is_rx_notassoc, st->is_rx_unencrypted,
		    st->is_rx_wepfail, st->is_rx_decap, st->is_rx_decryptcrc, st->is_rx_unauth, st->is_ccmp_replays,
		    st->is_ccmp_dec_errs, st->is_ht_rx_frame_below_ba_winstart, st->is_ht_rx_frame_above_ba_winend,
		    st->is_ht_rx_ba_window_slide, st->is_ht_rx_ba_window_jump, st->is_ht_rx_ba_no_buf,
		    st->is_ht_rx_ba_frame_lost, st->is_ht_rx_ba_window_gap_timeout, st->is_ht_rx_ba_timeout);
		break;
	}
	case 28: {	/* YYavic: RX BA durumu (TID 0..7): durum/basla/bas/bosluk/buf/gapto */
		struct ieee80211_node *ni = sc->sc_ic.ic_bss;
		int o = 0;
		if (ni == NULL) { snprintf(out, outlen, "bss yok"); break; }
		for (int t = 0; t < 8 && o < (int)outlen; t++) {
			struct ieee80211_rx_ba *ba = &ni->ni_rx_ba[t];
			if (ba->ba_state == 0)
				continue;
			int nb = 0;
			for (int i = 0; ba->ba_buf != NULL && i < IEEE80211_BA_MAX_RX_WINSZ; i++)
				if (ba->ba_buf[i].m != NULL)
					nb++;
			o += snprintf(out + o, outlen - o, "t%d s%d z%u w%u h%u g%d b%d p%d | ", t, ba->ba_state,
			    ba->ba_winsize, ba->ba_winstart, ba->ba_head, ba->ba_gapwait, nb, timeout_pending(&ba->ba_gap_to) ? 1 : 0);
		}
		if (o == 0)
			snprintf(out, outlen, "BA oturumu yok");
		break;
	}
	case 29: {	/* YYavic: kancaya verilen cerceve; a=1 -> tum TID'lerde bosluk atla + bosalt */
		int skipped = 0;
		if (a == 1 && sc->sc_ic.ic_bss != NULL) {
			struct ieee80211_node *ni = sc->sc_ic.ic_bss;
			struct mbuf_list ml = MBUF_LIST_INITIALIZER();
			for (int t = 0; t < 8; t++) {
				struct ieee80211_rx_ba *ba = &ni->ni_rx_ba[t];
				if (ba->ba_state != IEEE80211_BA_AGREED)
					continue;
				skipped += ieee80211_input_ba_gap_skip(ba);
				ieee80211_input_ba_flush(&sc->sc_ic, ni, ba, &ml);
			}
			if_input(&sc->sc_ic.ic_if, &ml);
		}
		snprintf(out, outlen, "hook_frames=%u skipped=%d rxfill_fail=%u rxfill_retry=%u rxq prod=%u cons=%u",
		    (unsigned)yy_rx_hook_frames, skipped, gYYRxFillFail, gYYRxFillRetry, sc->sc_rxq.mq_prod, sc->sc_rxq.mq_cons);
		break;
	}
	case 30: {	/* YYavic: HW cozmeyen cerceveler + anahtar durumu */
		struct ieee80211com *ic = &sc->sc_ic;
		struct ieee80211_node *ni = ic->ic_bss;
		struct ieee80211_stats *st = &ic->ic_stats;
		int o = snprintf(out, outlen, "cm=%u clm=%u rxd1=%08x | sw u=%u m=%u kid=%u fc=%04x | eapol=%u rep=%u",
		    gYYRxCM, gYYRxCLM, gYYRxLastRxd1, yy_wf_ucast, yy_wf_mcast, yy_wf_keyid, yy_wf_last_fc,
		    st->is_rx_eapol_key, st->is_rx_eapol_replay);
		if (ni != NULL && o < (int)outlen)
			o += snprintf(out + o, outlen - o, " | pk c%d f%x id%d", ni->ni_pairwise_key.k_cipher,
			    ni->ni_pairwise_key.k_flags, ni->ni_pairwise_key.k_id);
		for (int i = 0; i < 4 && o < (int)outlen; i++)
			if (ic->ic_nw_keys[i].k_cipher != IEEE80211_CIPHER_NONE)
				o += snprintf(out + o, outlen - o, " g%d c%d f%x", i, ic->ic_nw_keys[i].k_cipher,
				    ic->ic_nw_keys[i].k_flags);
		if (o < (int)outlen)
			snprintf(out + o, outlen - o, " tx%d", ic->ic_def_txkey);
		break;
	}
	case 31: {	/* YYavic test: anahtarlari donanima yeniden yukle */
		struct ieee80211com *ic = &sc->sc_ic;
		struct ieee80211_node *ni = ic->ic_bss;
		int rp = -1, rg = -1;
		if (ni != NULL && ni->ni_pairwise_key.k_cipher != IEEE80211_CIPHER_NONE)
			rp = (*ic->ic_set_key)(ic, ni, &ni->ni_pairwise_key);
		if (ni != NULL && ic->ic_def_txkey >= 0 && ic->ic_def_txkey < IEEE80211_GROUP_NKID &&
		    ic->ic_nw_keys[ic->ic_def_txkey].k_cipher != IEEE80211_CIPHER_NONE)
			rg = (*ic->ic_set_key)(ic, ni, &ic->ic_nw_keys[ic->ic_def_txkey]);
		snprintf(out, outlen, "set_key pairwise=%d group=%d", rp, rg);
		break;
	}
	case 32: {	/* YYavic perf: radyo is dongusu asama sureleri (ms), a=1 sifirla */
		uint64_t ns;
		absolutetime_to_nanoseconds(1000000, &ns);	/* 1 ms in abs units: ns per 1e6 abs */
#define YYMS(x) ((uint32_t)(((x) * ns / 1000000) / 1000000))
		snprintf(out, outlen, "intr=%ums/%u deq=%u fill=%u proc=%u ifin=%u | rx pkts=%u calls=%u | start=%ums/%u txdone=%u",
		    YYMS(gYYPfIntr), gYYPfIntrN, YYMS(gYYPfDeq), YYMS(gYYPfFill), YYMS(gYYPfProc), YYMS(gYYPfIfIn),
		    gYYPfRxPkts, gYYPfRxCalls, YYMS(gYYPfStart), gYYPfStartN, YYMS(gYYPfTxDone));
#undef YYMS
		if (a == 1) {
			gYYPfIntr = gYYPfDeq = gYYPfFill = gYYPfProc = gYYPfIfIn = gYYPfStart = gYYPfTxDone = 0;
			gYYPfIntrN = gYYPfRxPkts = gYYPfRxCalls = gYYPfStartN = 0;
		}
		break;
	}
	case 26: {	/* YYavic: MSI durumu */
		snprintf(out, outlen, "intr_index=%d intr_type=0x%x msi_count=%u intr_calls=%u poll_fires=%u poll_us=%u",
		    gCompatIntrIndex, gCompatIntrType, (unsigned)gCompatIntrCount, (unsigned)gMwxIntrCalls,
		    (unsigned)gMwxPollFires, (unsigned)gMwxPollUs);
		break;
	}
	case 22:	/* yoklama araligi (us), 100..20000 */
		if (a >= 100 && a <= 20000)
			gMwxPollUs = a;
		snprintf(out, outlen, "poll_us=%u fires=%u", gMwxPollUs, gMwxPollFires);
		break;
	case 19:	/* TX sayaclari */
		snprintf(out, outlen, "deq=%u ok=%u notxwi=%u ringfull=%u mapfail=%u oact=%u restart=%u freeev=%u freed=%u txwi_use=%d peak=%d bp=%d oactive=%u slots=%d",
		    gTxDeq, gTxOk, gTxNoTxwi, gTxRingFull, gTxMapFail, gTxOactSet, gTxRestart, gTxFreeEv, gTxFreed,
		    gTxwiUse, gTxwiPeak, gTxBackpressure, ifq_is_oactive(&sc->sc_ic.ic_if.if_snd),
		    mwx_dma_free_slots(&sc->sc_txq));
		break;
	case 20:	/* geri basinc ac/kapat */
		gTxBackpressure = (int)a;
		snprintf(out, outlen, "backpressure=%d", gTxBackpressure);
		break;
	case 21:	/* TX sayaclarini sifirla */
		gTxDeq = gTxOk = gTxNoTxwi = gTxRingFull = gTxMapFail = gTxOactSet = gTxRestart = gTxFreeEv = gTxFreed = 0;
		gTxwiPeak = gTxwiUse;
		snprintf(out, outlen, "tx sayaclari sifirlandi");
		break;
	case 33: {	/* YYavic HE durum; a=10 HE kapat, a=11 HE ac (sonraki baglantida), a=1 sayac sifirla */
		struct ieee80211com *ic = &sc->sc_ic;
		struct ieee80211_node *bni = ic->ic_bss;
		int o, k;
		if (a == 0x10) { gYYNoHe = 1; ic->ic_flags &= ~IEEE80211_F_HEON; }
		if (a == 0x11) { gYYNoHe = 0; ic->ic_flags |= IEEE80211_F_HEON; }
		if (a == 1) { gYYRxHe = 0; memset(gYYRxHeMcs, 0, sizeof(gYYRxHeMcs)); }
		o = snprintf(out, outlen, "heon=%d curmode=%u node_he=%d oper=0x%08x ap_mac=%02x%02x%02x%02x%02x%02x ap_phy0=0x%02x ap_mcs80=0x%04x | rx he=%u mcs:",
		    (ic->ic_flags & IEEE80211_F_HEON) != 0, ic->ic_curmode,
		    bni != NULL && (bni->ni_flags & IEEE80211_NODE_HE) != 0,
		    bni ? bni->ni_he_oper_params : 0,
		    bni ? bni->ni_he_cap_elem.mac_cap_info[0] : 0, bni ? bni->ni_he_cap_elem.mac_cap_info[1] : 0,
		    bni ? bni->ni_he_cap_elem.mac_cap_info[2] : 0, bni ? bni->ni_he_cap_elem.mac_cap_info[3] : 0,
		    bni ? bni->ni_he_cap_elem.mac_cap_info[4] : 0, bni ? bni->ni_he_cap_elem.mac_cap_info[5] : 0,
		    bni ? bni->ni_he_cap_elem.phy_cap_info[0] : 0,
		    bni ? le16toh(bni->ni_he_mcs_nss_supp.rx_mcs_80) : 0, gYYRxHe);
		for (k = 0; k < 12 && o < (int)outlen; k++)
			if (gYYRxHeMcs[k]) o += snprintf(out + o, outlen - o, " %u=%u", k, gYYRxHeMcs[k]);
		break;
	}
	case 34:	/* YYavic RX A-MSDU sayaclari; a=1 sifirla */
		if (a == 1)
			gYYAmsduSub = gYYAmsduMpdu = gYYAmsduSingle = gYYAmsduBad = gYYAmsduNoDa = gYYAmsduMaxN =
			    gYYAmsduFlushSeq = gYYAmsduFlushEnd = gYYAmsduFlushFull = gYYAmsduAllocFail = 0;
		{
			extern uint64_t gYYTxKicks, gYYTxKickRuns;	/* YYavicMtkWlan.cpp */
			snprintf(out, outlen, "sub=%u mpdu=%u single=%u maxn=%u bad=%u noda=%u flush seq=%u end=%u full=%u allocfail=%u | txkick=%llu runs=%llu",
			    gYYAmsduSub, gYYAmsduMpdu, gYYAmsduSingle, gYYAmsduMaxN, gYYAmsduBad, gYYAmsduNoDa,
			    gYYAmsduFlushSeq, gYYAmsduFlushEnd, gYYAmsduFlushFull, gYYAmsduAllocFail,
			    (unsigned long long)gYYTxKicks, (unsigned long long)gYYTxKickRuns);
		}
		break;
	case 35: {	/* YYavic 160 MHz: AP VHT operation + durum; a=10 160 kapat, a=11 ac (sonraki baglantida) */
		struct ieee80211com *ic = &sc->sc_ic;
		struct ieee80211_node *bni = ic->ic_bss;
		uint8_t c = 0, bw;
		if (a == 0x10) {
			ic->ic_vhtcaps &= ~(IEEE80211_VHTCAP_SUPP_CHAN_WIDTH_MASK | IEEE80211_VHTCAP_SHORT_GI_160);
			ic->ic_he_cap_elem.phy_cap_info[0] &= ~IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G;
		}
		if (a == 0x11) {
			ic->ic_vhtcaps |= IEEE80211_VHTCAP_SUPP_CHAN_WIDTH_160MHZ | IEEE80211_VHTCAP_SHORT_GI_160;
			ic->ic_he_cap_elem.phy_cap_info[0] |= IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G;
		}
		bw = mwx_bss_bw(sc, &c);
		snprintf(out, outlen, "our160=%d he160=%d | ap vht_op width=%u ccfs0=%u ccfs1=%u ap_vhtcaps=0x%08x ap_he_phy0=0x%02x | chw=%u bss_bw=%u center=%u cf1=%u",
		    (ic->ic_vhtcaps & IEEE80211_VHTCAP_SUPP_CHAN_WIDTH_MASK) != 0,
		    (ic->ic_he_cap_elem.phy_cap_info[0] & IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G) != 0,
		    bni ? bni->ni_vht_chanwidth : 0, bni ? bni->ni_vht_chan1 : 0, bni ? bni->ni_vht_chan2 : 0,
		    bni ? bni->ni_vhtcaps : 0, bni ? bni->ni_he_cap_elem.phy_cap_info[0] : 0,
		    bni ? bni->ni_chw : 0, bw, c,
		    (bni && bni->ni_chan && bni->ni_chan != IEEE80211_CHAN_ANYC) ? bni->ni_chan->ic_center_freq1 : 0);
		break;
	}
	case 37: {	/* YYavic 0.7.1 country: a=0 show; a='T'<<8|'R' (0x5452), 0x3030 = '00'.
			 * Only with Wi-Fi off (INIT); used from the next Wi-Fi on. */
		struct ieee80211com *ic = &sc->sc_ic;
		unsigned int i, n2 = 0, n5 = 0;
		const char *note = "";
		if (a != 0) {
			if (ic->ic_state != IEEE80211_S_INIT) {
				note = " (busy: turn Wi-Fi off first)";
			} else {
				char cc[3] = { (char)((a >> 8) & 0xff), (char)(a & 0xff), 0 };
				mwx_reg_set(sc, cc);
				mwx_reg_channels(sc);
				ieee80211_channel_init(&ic->ic_if);
				ieee80211_setmode(ic, (enum ieee80211_phymode)ic->ic_curmode);
				note = " (set; Wi-Fi on to apply)";
			}
		}
		for (i = 0; i <= IEEE80211_CHAN_MAX; i++) {
			if (ic->ic_channels[i].ic_flags == 0 || isclr(ic->ic_chan_active, i))
				continue;
			if (IEEE80211_IS_CHAN_2GHZ(&ic->ic_channels[i]))
				n2++;
			else
				n5++;
		}
		snprintf(out, outlen, "country=%s region=%s active 2g=%u 5g=%u%s",
		    sc->sc_alpha2, mwx_reg_names[sc->sc_reg], n2, n5, note);
		break;
	}
	case 36:	/* YYavic BSS RLM: a=rx_streams b=ht_op_info (0xff = varsayilan); sonraki baglantida */
		gYYRlmRx = (uint8_t)a;
		gYYRlmHtOp = (uint8_t)b;
		snprintf(out, outlen, "rlm rx_streams=%s0x%x ht_op_info=%s0x%x (mask=0x%x streams=%u)",
		    gYYRlmRx == 0xff ? "def/" : "", gYYRlmRx, gYYRlmHtOp == 0xff ? "def/" : "", gYYRlmHtOp,
		    sc->sc_capa.antenna_mask, sc->sc_capa.num_streams);
		break;
	case 13:	/* SET_RX_PATH simdi gonder */
		ret = (uint32_t)mt7921_mcu_set_chan_info(sc, MCU_EXT_CMD_SET_RX_PATH);
		snprintf(out, outlen, "SET_RX_PATH rv=%d", (int)ret);
		break;
	default:
		snprintf(out, outlen, "bilinmeyen op %u", op);
		ret = 0xffff;
	}
	smtw_trace("DBG op=%u a=%x b=%x -> %s\n", op, a, b, out);
	return ret;
}
