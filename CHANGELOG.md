# Changelog

Speeds are Chrome speedtest results (download / upload, Mbit/s) on the test
machine (Tongfang-chassis R9T Slayer laptop, AMD Ryzen 9 8945HX, macOS 15.8.1,
MT7922) with a Wi-Fi 6 router on 5 GHz channel 56, over an ISP line of
1 Gbit/s download / 50 Mbit/s upload (uploads are capped by the line).
"nQ" = macOS `networkQuality -I en3` download.

## 0.7.2 - 2026-10-26 - Bluetooth set up by the kext

- The kext now does the Bluetooth USB ID mask itself: the 27 MT7922 /
  MT7922A Bluetooth IDs in Linux `btusb` (incl. `13D3:3585`) appear to
  macOS's `bluetoothd` as `0489:E0D8`, its own MT7922 ID. `MtkBtIdMask.kext`
  is only needed for IDs that are not in that list.
- On an MT7922 card the kext sets the NVRAM values
  `bluetoothInternalControllerInfo` / `bluetoothExternalDongleFailed` at boot
  (only when missing or different); no OpenCore NVRAM entries needed.
  Result in the registry property `yyavic-bt`; off with `-yyavicnobt`.
- `Bluetooth/setup-bluetooth.sh`: one-time firmware copy to `/etc/bluetool`.
- Tested: with `MtkBtIdMask.kext` and the OpenCore NVRAM entries removed,
  Bluetooth comes up as `SRS_7922` over USB; without BlueToolFixup it does
  not (so BlueToolFixup stays required).

## 0.7.1 - 2026-10-26 - RZ616 ID, country setting

- **AMD RZ616 `14c3:0616`** is matched and run as an MT7922 (MT7922
  firmware, 160 MHz), as in Linux `mt7921/pci.c`. The chip id register now
  decides MT7922 vs. MT7921 (Linux `is_mt7922()`), the PCI ID only gives the
  default. Untested on a real `0616` card.
- **No more fixed Turkish channel list.** The country comes from the new
  boot-arg `yyavic-cc=XX`; channel sets for ETSI (EU/EEA, CH, GB, TR), FCC
  (US), Japan and China, otherwise the channels common to all of them
  (2.4 GHz 1-11, 5 GHz 36-64). The rules now apply in the radio itself, so
  the firmware channel domain, the scan and the list macOS gets are the same.
  Before, only the macOS list was filtered, and the radio still scanned
  channels 14 and 144-173. The country code goes to the firmware (channel domain, TX
  power) and to macOS (country code, channel info) instead of a fixed `TR`.
- `mtkscan dbg 37`: shows the country; `dbg 37 <hex>` changes it while Wi-Fi
  is off (e.g. `5553` = `US`, `3030` = default), used from the next Wi-Fi on.
- README: supported hardware with PCI / USB IDs, how to find yours.
- System Information → Wi-Fi → Firmware Version shows the real driver
  version (it was a fixed "0.7.0" string); it now comes from the build.

## 0.7.0 - 2026-10-23 - first public release - 913 / 47

Everything below, plus:

- Safety: the native layer starts only on macOS 15.8.1 (Darwin 24.6, build
  24H32), the build its IO80211/Skywalk class layouts were made for. On other
  builds it disables itself instead of panicking (`-yyavicnoverify` to force).
  macOS publishes the build number a little after the driver loads, so the
  layer waits for it (about 10-15 s on the test machine).
- 160 MHz is advertised only on MT7922 (Linux does the same); MT7921/MT7921K
  stay at 80 MHz.
- Removed a runtime debug trigger that could replay deferred WCL
  notifications while connected (it panicked WCLNetManager).
- Release build without build-machine paths (prefix maps, `strip -S`);
  portable build scripts (`DEVELOPER_DIR`, `MACKERNELSDK`).
- Bluetooth: setup guide in the README plus `Bluetooth/MtkBtIdMask.kext`
  (USB ID mask so macOS's own `bluetoothd` uses its MT7922 path) and the
  MT7922 Bluetooth firmware for `/etc/bluetool`.

## 0.6.9 - 160 MHz on by default - **920 / 46**

## 0.6.8 - 160 MHz fixed

- BSS RLM command now matches Linux `mt76_connac_mcu_uni_set_chctx`:
  `rx_streams` is the RX chain mask (0x3), not the stream count (2), and
  `ht_op_info = 4` for 40 MHz and wider. With the old values 160 MHz ran at
  208-284 Mbit/s (nQ) with many HE-MCS 0-3 frames; with the new ones the link
  is 2402 Mbit/s, all frames HE-MCS 11, nQ 717-758.
- 160 MHz can be switched at runtime (`mtkscan dbg 35 10/11`, then Wi-Fi
  off/on); BSS RLM overrides (`dbg 36`).

## 0.6.7 - 160 MHz capabilities

- VHT `SUPP_CHAN_WIDTH_160` + short GI 160, HE 160 MHz bits and HE-MCS map for
  160, `STA_REC_HE` BW160 map. `dbg 35` shows the AP's VHT operation
  (width, CCFS0/1).

## 0.6.6 - asynchronous TX - 770 / 46, nQ 697

- TX from the native layer no longer waits on the radio's command gate: frames
  are queued and an event source on the radio work loop sends them. Layer TX
  cost went from ~19 µs to ~1 µs per packet (TCP ACKs during downloads).

## 0.6.5 - larger rings - 770 / 47

- TXWI pool 512 → 4096 (it ran out under TCP ACK load: 0.45 % of ACKs were
  dropped), TX ring 256 → 2048, RX ring 256 → 1536 (Linux `mt7921.h` sizes),
  send queue 512.

## 0.6.4 - A-MSDU fix - 760 / 47

- An A-MSDU split across two RX batches was delivered half; the rest was then
  dropped below the BlockAck window. Open A-MSDUs now wait for their remaining
  subframes (timeout via the poll timer).

## 0.6.3 - RX A-MSDU - 718 / 47

- The hardware splits A-MSDUs into subframes (RXD4 first/mid/last); the radio
  joins them back into one A-MSDU MPDU so net80211 reorders and de-aggregates
  it normally. Advertises 11454-byte VHT MPDUs, HT A-MSDU 7935 and A-MSDU in
  A-MPDU.
- Boot-time `APPLE80211_M_DRIVER_AVAILABLE` event (as Apple's Broadcom driver).

## 0.6.1 - BlockAck 256, VHT capabilities - 732 / 47

- RX BlockAck reorder window 64 → 256 (HE).
- VHT: 1 MB A-MPDU, LDPC, RX STBC, SU/MU beamformee (Linux mt7921 set).

## 0.6.0 - Wi-Fi 6 (802.11ax) - 576 / 47

- HE capabilities as Linux `mt7921_init_he_caps`, firmware `STA_REC_HE` /
  `STA_REC_HE_V2`, BSS HE basic + BSS color, AX PHY modes, HE RX rate decoding
  and HE link rates in macOS.
- Full beacon elements passed to macOS (only the first 260 bytes were passed
  before, which cut off the HE elements).
- net80211: the HE capabilities element had no length byte when no PPE
  thresholds were present.

## 0.5.x - single kext - 430 / 47

- Radio and native layer linked into one kext, the layer starts on its own
  once the radio is ready (NVRAM guard, `-yyavicoff`).
- Batched TX, correct link width/rate/MCS in System Information, regulatory
  channel list, private Wi-Fi address, Wi-Fi on/off from the menu.

## 0.4.x - native data path

- Skywalk RX/TX data path (DHCP, internet). Stability fixes in the radio: RX
  ring refill after mbuf allocation failure, TX ring writes serialized on the
  command gate (stalls under load).

## 0.3.x - native join

- WCL join (WPA2-PSK with the PMK from macOS), BSS info / extended BSS info,
  4-way handshake events.

## 0.2.x - native scan

- WCL scan results (beacon messages), channel list.

## 0.1.x - IO80211 bring-up

- `IO80211Controller` / `IO80211InfraInterface` / Skywalk interface for macOS
  15.8.1 from generated, layout-verified headers; the card appears as a
  native Wi-Fi port.

## Radio work before the native layer

On the YYavicMtkWlan radio (then used through its own Ethernet-type
interface): firmware load and interrupts (MSI), DMA scatter/gather limit, data
path, 802.11n with RX BlockAck, VHT 80 MHz, TX rate selection bug (all TX at
6 Mbit/s), TX A-MPDU.
