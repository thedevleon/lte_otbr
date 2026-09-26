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

**Status 2026-09-26: builds, links, and boots clean with populated storage.** FLASH ~733K
(47% of 1524K), RAM fits after the net_buf 40+40 → 32+32 cut. One benign
warning remains: `-Waddress-of-packed-member` on `aMessageInfo->mPeerAddr` in
`modules/openthread/platform/udp.c` (byte-wise reads only, safe on Cortex-M33).

On-target verified: PPP up (CGNAT IPv4), Thread auto-starts to `leader` (workqueue),
`ot br state` = running, on-mesh prefix + GUA published, and **NAT64 fully active**:
`PrefixManager: Active` + `Translator: Active` (needed the two fixes below). Stack
headroom after boot (`kernel thread stacks`): main peak 3348/6144 — the boot-time CRACEN
chain needs ~3.3K, so 3072 was indeed doomed; sysworkq 1364/4096 (runs `openthread_run()`);
all other threads ≤ 41%.

The two NAT64 fixes: `start_nat64_service()` in
`openthread_border_router.c` no longer requires a gateway address (PPP is point-to-point,
gateway is 0.0.0.0 — that check had blocked `otNat64SetIp4Cidr()` entirely), and `src/main.c`
installs an IPv4 default route on the PPP iface (gw = own address, `net_if_set_default()`)
so NAT64 raw-socket egress doesn't misroute to thread0.

**Ordering gotcha (fixed and verified):** Thread must start only *after* the PPP AIL has its
IPv4 address.
Starting it at boot (~0.5 s, IPCP finishes ~5-8 s later) left `ot netdata show` completely
empty — no on-mesh prefix, no NAT64 prefix, no GUA on thread0 — even though `ot br state`
and `ot nat64 state` looked active. The app now polls once per second and starts Thread
(via the workqueue) only when the PPP iface has a global IPv4. Note:
`NET_EVENT_IPV4_ADDR_ADD` proved unreliable for triggering this (handler never observed to
fire for the PPP iface) — hence polling, not events.

With the ordering fix, network data is fully published (verified 2026-09-26): on-mesh
prefix `fd4b:1551:f28:1::/64` (paos), ULA default route `fc00::/7`, and **NAT64 prefix
`fd4b:1551:f28:2::/96`** (flag `s`); thread0 gets a GUA, ppp0 shows gw = own address and is
the default interface. These prefixes are randomly generated when the BR forms the network,
so they change after a storage erase or factory reset — always read the current values from
`ot netdata show`, don't hardcode them. Ping target for 8.8.8.8 through NAT64:
`<nat64prefix>:0808:0808` (e.g. `fd4b:1551:f28:2:0:0:0808:0808`).

**End-to-end verified 2026-09-26:** a Thread ED (nRF52840 dongle RCP + `ot-cli` on the Linux
host, Spinel/HDLC at 115200) joined, got a GUA, pinged 8.8.8.8 through NAT64 over LTE
(~150 ms, 0% loss), and resolved `google.com` via the BR's DNS upstream forwarding
(ED auto-discovers the BR as DNS server from the SRP service in netdata; BR log shows
`DnssdServer: Received query` → `Upstream query transaction ... completed` over PPP).
The SRP server initially would not start — boot log showed
`SrpServer: Failed to prepare socket: InvalidState` in a retry loop. Root cause:
`CONFIG_OPENTHREAD_ZEPHYR_BORDER_ROUTER_MAX_UDP_SERVICES=5` (default) is exhausted by
TMF/DNS-SD/Border-Agent/mDNS sockets, so `otPlatUdpSocket()` rejects the SRP server's socket
with `OT_ERROR_INVALID_STATE`; bumped to 8 in `prj.conf`. Raising that cap requires
`CONFIG_ZVFS_POLL_MAX=20` too — with 8 UDP services the socket service needs 17 poll
entries, and with the default 16 the `net_sock_svc` thread fails to start at all, which
also kills PPP DNS setup and the NAT64 translator socket (observed on-target). Note the SRP server state machine
also wedges in `Stopped` after such a failure (`Enable()` only runs from `Disabled`), and
`ot srp server enable` prints `Done` regardless because the API returns void — check
`ot srp server state`, not the CLI's `Done`. For OT logs set
`CONFIG_OPENTHREAD_DEBUG=y` + `CONFIG_OPENTHREAD_LOG_LEVEL_INFO=y` (the level choice depends
on the former; removed from `prj.conf` after diagnosis).

Two warts seen during the DNS test, benign so far but worth knowing: `Nat64: no mapping
found for the IPv4 address` + `openthread_nat64_send error 2` warnings fire when the
upstream DNS query egresses (query still completes), and the upstream resolver returns AAAA
records for dual-stack names — on the IPv4-only LTE uplink those IPv6 addresses are
unreachable, so real ED applications should query A records and use the NAT64 prefix
(the OT DNS client `Nat64Mode` handles this).

**Leadership gotcha:** if another leader with the same dataset is alive when the BR boots
(e.g. the host ot-cli dongle left running from a test), the partitions merge and the BR
demotes to a *child* — BR features keep half-working (NAT64 translator activates, SRP
listens) but DUA registration fails and the topology is wrong. Two defenses: the app now
sets `otThreadSetLocalLeaderWeight(UINT8_MAX)` before `openthread_run()` so the BR wins
merges, and in tests always let the BR become `leader` first (`thread stop` on the other
device, reboot BR, check `ot state`, then `thread start` the joiner).

**Boot-hang gotcha (observed on-target 2026-09-26, root-caused via J-Link):** once the
storage partition (0x174000, 36K ZMS for settings/secure-storage) holds data, the boot-time
secure-storage AEAD key derivation runs the deep CRACEN/sxsymcrypt chain
(`its_transform_aead_get_key_huk` → `hw_unique_key_derive_key` → SP800-108 CTR KDF →
`sx_cmdma_*`) on the main stack during sysinit. With `MAIN_STACK_SIZE=3072` this overflows,
the ARMv8-M PSPLIM guard raises `K_ERR_STACK_CHK_FAIL`, the fatal handler halts the CPU,
and because NCS routes printk through deferred logging the buffered boot banner is never
flushed — the board looks completely dead (no UART, no LED). First boot after flashing
always works (empty storage), every later boot dies. Fixes: `MAIN_STACK_SIZE=6144`
(paid for by cutting net_buf 40+40 to 32+32), Thread start moved to the system workqueue
(`start_thread_work` in `src/main.c`, 4096-byte stack — never run `openthread_run()` on the
main stack, and keep `CONFIG_OPENTHREAD_MANUAL_START=y` so it isn't run during sysinit
either). Field recovery without reflashing: erase the storage partition over J-Link
(`JLinkExe -device NRF54L15_M33`, `erase 0x174000 0x17CFFF`) and the old firmware boots
once more. Diagnosis recipe: JLinkExe `h`/`regs`, PC resolves to `arch_system_halt` via
addr2line, R0 = fatal reason (2 = stack check fail), unwind ESF at PSP for the faulting PC.

## Bring-up checklist (on-target shell, uart20 115200)

1. `net iface` — expect the OpenThread iface and a PPP iface (with an IPv4 address, e.g.
   10.x CGNAT). If PPP has no address, check registration: `at at+cereg?`, `at at+cgpaddr`.
2. `ot state` — should become `leader` on its own ~5-10 s after boot (the app starts Thread
   via `openthread_run()` only once the PPP iface has its IPv4 address; watch for
   `PPP IPv4 up, default route set, starting Thread` in the log).
3. `ot br state`, `ot nat64 state` — border routing + NAT64 should be running once PPP is up
   (`Translator: Active` after the gateway-check fix; services start on `NET_EVENT_IF_UP` of
   the AIL). `ot nat64prefix` is an InvalidCommand on this fork's CLI — read the published
   NAT64 prefix from `ot netdata show` instead.
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
  ~74K bss): `NET_TCP=n`, heap 40K, mbedtls heap 2K, OT message buffers 96 (Kconfig floor),
  net_buf 32+32, net_pkt 14+14, `NRF_802154_RX_BUFFERS=12`, reduced stacks everywhere —
  except `MAIN_STACK_SIZE=6144`, which is load-bearing (boot-time CRACEN key derivation,
  see the gotcha in the status section).
  Think twice before raising any buffer/stack/heap setting — check
  the overflow with a build first. Flash has headroom (~750K of 1.5M used).
- `boards/nrf93m1dk_nrf54l15_cpuapp.conf` — UART async + CMUX sizing (required with PM runtime),
  carrier APN `simbase`.
- `boards/nrf93m1dk_nrf54l15_cpuapp.overlay` — enables `uart30` (115200, HWFC) + `modem`,
  `rng` as entropy source (`psa_rng` disabled), `xo`/`lfclk`.
- `src/main.c` — brings the PPP interface up (found by L2 type, never by fixed index), logs
  net_mgmt connectivity events, heartbeat on `led2` (green, P2.10). Once per second the main
  loop checks whether the PPP iface has a global IPv4 yet; when it does, it installs an IPv4
  default route (gateway = own address — PPP is point-to-point so the value is a dummy;
  without a default route the NAT64 raw-socket egress would misroute to the default thread0
  iface), makes PPP the default interface, and starts the Thread stack from the **system
  workqueue** (`start_thread_work` → `openthread_run()`; the main stack is too small for OT
  API calls, see the gotcha in the status section). Thread must start after the AIL is
  ready or the BR never publishes prefixes to network data (ordering gotcha, status
  section). Border router services are
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

- `ot state`, `ot dataset active`, `ot br state`, `ot nat64 state`, `ot netdata show`
  (`ot nat64prefix` is an InvalidCommand on this fork's CLI)
- `net iface`, `net ipv6`, `net ppp` — network/PPP state
- `at` — raw AT commands to the modem (over CMUX user pipe, works while PPP is up)

## Conventions / gotchas

- **C style:** tabs for indentation, Zephyr/Nordic idioms.
- Board is very new (nrf93m1dk exists only on NCS `main`); don't assume APIs from tagged NCS releases.
- Kconfig is plain (`CONFIG_X=y`); board conf applies only to the `nrf93m1dk/nrf54l15/cpuapp` target.
- There is no test suite, CI, or lint configuration — verification is build + on-target shell
  interaction via the VS Code extension's serial terminal (uart20, 115200).
