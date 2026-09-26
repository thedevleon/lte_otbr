# lte_router — OpenThread Border Router on nRF93M1 DK

OpenThread Border Router (OTBR) firmware running **entirely on the nRF54L15** of the
nRF93M1 DK, using the on-board **nRF93M1 LTE Cat 1 bis modem** as the internet backhaul.
No Linux host, no RCP split: OpenThread runs in FTD SoC mode on the nRF54L15's internal
802.15.4 radio, and the LTE link is a native Zephyr PPP interface acting as the border
router's backbone (AIL).

Thread end devices join the mesh, get a global address (GUA) via SLAAC, auto-discover the
router as their DNS server, and reach the IPv4 internet through OpenThread's NAT64
translator over the carrier's CGNAT LTE connection.

Verified end-to-end: an end device pinged `8.8.8.8` and resolved `google.com` through the
router (~150 ms, 0% loss).

## Features

- OpenThread FTD border router (Thread 1.4) on the internal 802.15.4 radio
- LTE Cat 1 bis backhaul via PPP over UART + CMUX (`modem_cellular` driver)
- Border routing: on-mesh prefix (OMR) + GUA assignment via SLAAC
- NAT64 translator: Thread nodes reach IPv4 hosts via the published NAT64 /96 prefix
- DNS upstream forwarding: Thread DNS queries resolved via the LTE-provided DNS servers
- SRP server + DNS-SD server: zero-config DNS server discovery for end devices
- Border Agent with ephemeral key (ePSKc) for external commissioning
- Automatic bring-up: LTE first, Thread starts once the backbone has its IPv4 address

## Architecture

```
 Thread mesh                    nRF93M1 DK                       Internet
┌──────────────┐   802.15.4  ┌─────────────────────────┐  LTE   ┌──────────┐
│  End device  │◄───────────►│ nRF54L15 (this firmware)│◄──────►│ IPv4     │
│  (GUA, DNS)  │             │  OT FTD + border router │  PPP/  │ hosts    │
└──────────────┘             │  NAT64, SRP, DNS fwd    │  CMUX  │ (CGNAT)  │
                             └───────────┬─────────────┘        └──────────┘
                                         │ UART30 + CMUX
                             ┌───────────▼─────────────┐
                             │ nRF93M1 LTE Cat 1 bis   │
                             │ (stock modem firmware)  │
                             └─────────────────────────┘
```

- **Outbound:** Thread node sends IPv6 to `<NAT64 prefix>:<IPv4>` → OT NAT64 translator →
  raw IPv4 socket → PPP → LTE → internet.
- **Inbound:** replies match the translator's flow mapping and are forwarded back into the
  mesh. Unsolicited inbound traffic is impossible (CGNAT) — all traffic is device-initiated.
- **DNS:** the BR's DNS-SD server receives queries on its mesh-local address, forwards them
  upstream to the PPP-provided DNS servers (8.8.8.8 / 1.1.1.1), and returns the answers.

## Hardware

- Nordic **nRF93M1 DK** (`nrf93m1dk/nrf54l15/cpuapp`, tested at board revision 0.4.0)
- SIM card with a data plan (tested with APN `simbase`; CGNAT IPv4-only PDP context)

## Prerequisites

- nRF Connect SDK `main` checkout (the `nrf93m1dk` board only exists there)
- Zephyr SDK toolchain (`arm-zephyr-eabi-gcc`)
- Two **local, uncommitted NCS patches** — see `patches/` (apply with `git apply` in the
  `zephyr` and `nrf` trees if your checkout doesn't have them):
  - `patches/zephyr-ppp-ail.patch` — allows PPP as the OTBR backbone interface and shims
    the Zephyr OTBR glue against the older bundled OpenThread fork
  - `patches/nrf-openthread-br-platform.patch` — compiles the border-router platform
    sources into the NCS OpenThread module

## Build and flash

```sh
west build --build-dir build . --pristine --board nrf93m1dk/nrf54l15/cpuapp
west flash
```

(Or use the nRF Connect for VS Code extension.)

The console/shell is on **uart20 at 115200**.

## First boot

Expected sequence (all automatic):

1. `Bringing up PPP interface 2` — LTE attach + PDP context (~5 s)
2. `PPP IPv4 up, default route set, starting Thread`
3. `ot state` → `leader`
4. `ot nat64 state` → `PrefixManager: Active`, `Translator: Active`
5. `ot srp server state` → `running`
6. `ot netdata show` → on-mesh prefix, `fc00::/7` route, NAT64 /96 prefix, DNS/SRP service

If anything deviates, consult `AGENTS.md` — it documents every failure mode observed
during bring-up (including a J-Link recovery recipe) with root causes.

## Testing with an end device

Any Thread FTD/MTD works. A convenient option is an nRF52840 dongle with RCP firmware
driven by the OpenThread POSIX CLI on a Linux host
(`ot-cli 'spinel+hdlc+uart:///dev/ttyACM0?uart-baudrate=115200'`).

Join with the default dataset (from `prj.conf`):

```
dataset networkname ot_lte_br
dataset channel 11
dataset panid 43981
dataset extpanid dead00beef00cafe
dataset networkkey 00112233445566778899aabbccddeeff
dataset commit active
ifconfig up
thread start
```

Then on the end device:

```
state                  # router or child
ipaddr                 # should contain a GUA from the BR's on-mesh prefix
dns config             # server auto-discovered: the BR's mesh-local address
dns resolve google.com
ping <nat64prefix>:0:0:0808:0808     # 8.8.8.8 via NAT64 (bytes are hex: 08.08.08.08)
```

Read the current prefixes from `ot netdata show` on the BR — they are randomly generated
when the network forms and change after a factory reset.

## Limitations (by design)

- **IPv4-only uplink:** the carrier PDP context is CGNAT IPv4. No inbound connectivity,
  no native IPv6 routing — everything goes through NAT64.
- **Device-initiated traffic only:** Thread nodes cannot be reached from the internet.
- **AAAA caveat:** upstream DNS returns AAAA records for dual-stack names, but those
  native IPv6 addresses are unreachable. Applications should use A records + the NAT64
  prefix (the OpenThread DNS client `Nat64Mode` handles this).
- If the subscription ever offers IPv6 PDP, the firmware can be slimmed to IPv6-only +
  DHCPv6-PD (see `AGENTS.md`).

## Project layout

- `prj.conf` — application Kconfig (networking, PPP/modem, OpenThread border router,
  RAM tuning — the nRF54L15's 256K RAM is nearly full, see comments)
- `boards/` — board-specific Kconfig (APN, UART/CMUX sizing) and devicetree overlay
- `src/main.c` — bring-up logic: PPP up → default route → Thread start (workqueue),
  leader weight, heartbeat LED, net event logging
- `patches/` — exported copies of the required NCS working-tree patches
- `support/` — nRF93M1 datasheet and cellular AT commands documentation (PDF)
- `AGENTS.md` — deep technical notes: architecture decisions, all bring-up pitfalls with
  root causes, on-target debugging recipes. Read this before changing anything.
