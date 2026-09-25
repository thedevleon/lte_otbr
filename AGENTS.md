# AGENTS.md — lte_router

OpenThread Border Router firmware for the **nRF93M1 DK** (`nrf93m1dk/nrf54l15/cpuapp`), built on
the nRF Connect SDK (Zephyr). The nRF54L15 runs OpenThread in FTD SoC mode on its internal
802.15.4 radio and routes the Thread mesh to the internet through the on-board nRF93M1 LTE
Cat 1 bis modem. The LTE link is a native PPP interface (net l2 PPP over UART + CMUX,
`modem_cellular` driver) and acts as the border router's backbone (AIL). Outbound connectivity
for Thread nodes is via the OpenThread NAT64 translator (the carrier provides only CGNAT IPv4);
all traffic is device-initiated.

## Environment (machine-specific)

- **NCS tree:** `/home/leon/ncs/main` (`main` branch checkout)
- **Toolchain:** `/home/leon/ncs/toolchains/8285d8ad56` (Zephyr SDK `arm-zephyr-eabi-gcc`, cmake, ninja, west)
- Shell env vars (`ZEPHYR_BASE` etc.) are **not set globally**; builds are normally launched by the nRF Connect for VS Code extension, which passes the paths.

## Required NCS patches (local, uncommitted)

Two working-tree patches are required; do not revert them. Exported copies live in `patches/`.

**`patches/zephyr-ppp-ail.patch`** — in `/home/leon/ncs/main/zephyr`. The Zephyr OTBR glue
upstream only accepts Ethernet/Wi-Fi as backbone interface, and (on NCS main) is written
against a newer upstream OpenThread than the bundled `sdk-openthread` fork
(ncs-thread-reference-20250402):

- `zephyr/subsys/net/l2/openthread/openthread_border_router.c` — `is_ail_l2()` helper accepts
  the PPP L2 as AIL (three event handlers) and only compares against the Ethernet L2 when
  `CONFIG_NET_L2_ETHERNET` is on (otherwise `_net_l2_ETHERNET` is an undefined symbol).
  The Ethernet checksum-offload helper is compiled out entirely without
  `CONFIG_NET_L2_ETHERNET` (`net_eth_get_hw_config` needs the Ethernet L2). The
  `otBorderAgentSetEnabled()` calls are removed (the fork's Border Agent starts
  automatically with Thread) and `otNat64ClearIp4Cidr()` is replaced by just disabling the
  translator. Feature-off guards: `trel_plat_init()` under `CONFIG_OPENTHREAD_TREL`,
  `dhcpv6_pd_client_init()`/`otBorderRoutingDhcp6PdSetEnabled()` under
  `CONFIG_OPENTHREAD_BORDER_ROUTING_DHCP6_PD`, all `otBackboneRouter*()` calls and the BBR
  multicast listener handler under `CONFIG_OPENTHREAD_BACKBONE_ROUTER`, and
  `openthread_border_router_add_or_rm_route_to_multicast_groups()` under
  `CONFIG_NET_ROUTE_MCAST` (the fork builds without these features, so the symbols don't
  exist).
- `zephyr/modules/openthread/platform/infra_if.c` — same compile-time Ethernet guard in
  `remove_checksums_for_eth_offloading()`; `otPlatGetInfraIfLinkLayerAddress()` removed
  (type/API does not exist in the fork, and nothing there calls it).
- `zephyr/modules/openthread/platform/udp.c` — `OT_NETIF_THREAD_HOST` cases mapped to
  `OT_NETIF_THREAD`, `OT_NETIF_THREAD_INTERNAL` cases dropped (fork enum has only
  UNSPECIFIED/THREAD/BACKBONE), `otIp6IsLinkLocalUnicast()` replaced by Zephyr's
  `net_ipv6_is_ll_addr()`.
- `zephyr/modules/openthread/platform/mdns_socket.c` — `mdns_plat_monitor_interface()`
  stubbed: the fork's older mDNS platform API has no
  `otPlatMdnsHandleHostAddressEvent/RemoveAll` host-address reporting.
- `zephyr/modules/openthread/platform/border_agent.c` — rewritten against the fork's older
  ePSKc API (`otBorderAgentSetEphemeralKey()` string API + `otBorderAgentSetEphemeralKeyCallback`
  + `otBorderAgentIsEphemeralKeyActive`); passcode is generated locally (8 random digits +
  Verhoeff check digit) since `otBorderAgentEphemeralKeyGenerateTap` does not exist.
  `otBorderAgentSetMeshCoPServiceBaseName()` / `otBorderAgentSetVendorTxtData()` calls removed
  (not in the fork).

**`patches/nrf-openthread-br-platform.patch`** — in `/home/leon/ncs/main/nrf`. In NCS the
`openthread` west module's CMake entry point is `nrf/modules/openthread/CMakeLists.txt`;
Zephyr's own `zephyr/modules/openthread/CMakeLists.txt` (and its `platform/` one) are **never
processed**. NCS's shadowed `nrf/modules/openthread/platform/CMakeLists.txt` cherry-picks
Zephyr platform sources but does not know the NXP border-router platform files. The patch adds
them (`infra_if.c`, `udp.c`, `mdns_socket.c`, `border_agent.c`, `trel.c`, `dhcp6_pd.c`,
`dns_upstream_resolver.c`) plus the needed include dirs under
`CONFIG_OPENTHREAD_ZEPHYR_BORDER_ROUTER`. `trel.c` and `dhcp6_pd.c` are deliberately omitted
(TREL and DHCPv6-PD are disabled and their sources need newer OT APIs). It also adds the OpenThread repo's `src` and
`src/core` include dirs (for `common/code_utils.hpp`, which the NXP platform files include):
upstream Zephyr gets these transitively because it links the OT libs with
`zephyr_link_libraries()` (usage requirements propagate through `zephyr_interface`), while
NCS uses `target_link_libraries(zephyr PRIVATE ...)`, which propagates nothing to module
libraries.

Related: `CONFIG_OPENTHREAD_ZEPHYR_BORDER_ROUTER_MDNS_AUTO_NAMING=n` in `prj.conf` because the
fork also lacks `otMdnsSetLocalHostName()`.

Keep the change as a working-tree modification or export it with
`git -C /home/leon/ncs/main/zephyr diff > patches/zephyr-ppp-ail.patch`.

## Build / flash

**Never run builds yourself — always ask the user to build** (they build via the nRF Connect
VS Code extension terminal). CLI equivalent:

```sh
west build --build-dir build . --pristine --board nrf93m1dk/nrf54l15/cpuapp
```

Flashing/debugging is done through the nRF Connect VS Code extension (nrfjprog/J-Link).

**Status 2026-09-26: builds and links clean.** FLASH 733540 B (47% of 1524K), RAM 259336 B
(98.9% of 256K — only ~2.8K slack, see the RAM warning under Project layout). One benign
warning remains: `-Waddress-of-packed-member` on `aMessageInfo->mPeerAddr` in
`modules/openthread/platform/udp.c` (byte-wise reads only, safe on Cortex-M33).

## Bring-up checklist (on-target shell, uart20 115200)

1. `net iface` — expect the OpenThread iface and a PPP iface (with an IPv4 address, e.g.
   10.x CGNAT). If PPP has no address, check registration: `at at+cereg?`, `at at+cgpaddr`.
2. `ot state` — should become `leader` (static dataset, single device).
3. `ot br state`, `ot nat64 state`, `ot nat64prefix` — border routing + NAT64 translator
   should be running once PPP is up (services start on `NET_EVENT_IF_UP` of the AIL).
4. Join a Thread end device (e.g. nRF52840-DK CLI sample, same dataset) — it should get a
   GUA from the BR's on-mesh prefix (`ot ipaddr` on the ED).
5. From the ED, ping a public IPv4 through the NAT64 prefix:
   `ot ping <nat64prefix>:0808:0808` (= 8.8.8.8). DNS: the BR forwards Thread DNS queries
   upstream via the PPP-provided DNS servers.
6. If the OTBR services don't start: watch the boot log for `openthread_border_router`
   errors (e.g. `otBorderRoutingInit` failing without a global IPv6 on the AIL — expected
   on IPv4-only LTE, NAT64 path should still work).

## Project layout

- `prj.conf` — app Kconfig: dual-stack networking, PPP + `modem_cellular`, OpenThread FTD with
  the Zephyr border router integration (`CONFIG_OPENTHREAD_ZEPHYR_BORDER_ROUTER`), NAT64
  translator, SRP server, DNS upstream forwarding, static Thread dataset, shells. TREL,
  DHCPv6-PD, SRP/mDNS proxies and backbone multicast routing are intentionally off (no LAN
  behind an LTE link). **RAM is extremely tight** (nRF54L15 has 256K; the OT instance alone is
  ~74K bss): `NET_TCP=n`, heap 48K, mbedtls heap 4K, OT message buffers 64, net_buf 48+48,
  reduced stacks everywhere. Think twice before raising any buffer/stack/heap setting — check
  the overflow with a build first. Flash has headroom (~750K of 1.5M used).
- `boards/nrf93m1dk_nrf54l15_cpuapp.conf` — UART async + CMUX sizing (required with PM runtime),
  carrier APN `simbase`.
- `boards/nrf93m1dk_nrf54l15_cpuapp.overlay` — enables `uart30` (115200, HWFC) + `modem`,
  `rng` as entropy source (`psa_rng` disabled), `xo`/`lfclk`.
- `src/main.c` — brings the PPP interface up (found by L2 type, never by fixed index), logs
  net_mgmt connectivity events, heartbeat on `led2` (green, P2.10). Border router services are
  started automatically by the Zephyr OTBR integration when the PPP interface comes up.
- `support/` — nRF93M1 datasheet v0.7 + cellular AT commands v1.0 PDFs. Consult these for modem
  behavior, not generic nRF91 assumptions.

## Carrier facts (probed 2026-09-25)

- SIM APN `simbase` (the `onomondo` APN also works), IPv4 PDP only: assigns a private address
  (CGNAT), e.g. `10.8.250.57`. No inbound connectivity.
- IPv6 PDP contexts are rejected by the network: `+CME ERROR: Requested service option not
  subscribed (#33)`. If the subscription ever gets IPv6, the firmware can be slimmed to
  IPv6-only + DHCPv6-PD (drop `NET_IPV4`/NAT64, set CGDCONT PDP type to `IPV6`).

## Useful on-target shell commands

- `ot state`, `ot dataset active`, `ot br state`, `ot nat64 state`, `ot nat64prefix`
- `net iface`, `net ipv6`, `net ppp` — network/PPP state
- `at` — raw AT commands to the modem (over CMUX user pipe, works while PPP is up)

## Conventions / gotchas

- **C style:** tabs for indentation, Zephyr/Nordic idioms.
- Board is very new (nrf93m1dk exists only on NCS `main`); don't assume APIs from tagged NCS releases.
- Kconfig is plain (`CONFIG_X=y`); board conf applies only to the `nrf93m1dk/nrf54l15/cpuapp` target.
- There is no test suite, CI, or lint configuration — verification is build + on-target shell
  interaction via the VS Code extension's serial terminal (uart20, 115200).
