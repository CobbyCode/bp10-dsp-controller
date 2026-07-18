# Ticket: SoftAP lifecycle repair

## Status

done

## Objective

Identify `ESP_014019` conclusively and repair the BP10 Wi-Fi lifecycle without hiding the SSID.

## Work

- Preserve all existing dirty DSP/UI changes.
- Confirm the visible SSID belongs to this ESP using BSSID/AP MAC or a controlled reboot/power-off observation.
- Log runtime Wi-Fi mode, STA link/IP, configured AP SSID, DHCP state, and setup/captive service state.
- Find every source of `ESP_014019`; ensure no legacy/parallel provisioning path remains.
- Use `bp10-xxxx` consistently.
- No credentials: APSTA with setup SoftAP and setup services.
- On `IP_EVENT_STA_GOT_IP`: stop AP DHCP and setup-only services cleanly, then switch to STA mode so no AP beacon remains.
- Factory reset: setup SoftAP returns.
- Persistent STA failure: recovery SoftAP returns after existing setup timeout.
- Add focused regression coverage where practical.
- Build, flash `/dev/ttyACM0`, and execute the requested real-device sequence. Never print WLAN credentials.

## Acceptance

- Factory reset: `bp10-xxxx` visible/connectable.
- Valid home WLAN: STA receives IP, AP disappears shortly after, home-LAN UI stays reachable.
- Reboot with saved credentials: no visible SoftAP.
- Invalid WLAN/timeout: recovery SoftAP visible/connectable.
- Report firmware ELF hash and concrete runtime evidence.

## Result

- `ESP_014019` conclusively belongs to this controller: external scan BSSID
  `24:EC:4A:01:40:19`; esptool chip/base MAC `24:EC:4A:01:40:18`. The AP MAC is
  the ESP-IDF-derived base+1 address. No `ESP_014019` string or second
  provisioning component exists in the repository; it was retained Wi-Fi AP
  driver/NVS configuration.
- Replaced the empty-SSID/APSTA workaround with a real `WIFI_MODE_STA`
  transition. On `IP_EVENT_STA_GOT_IP`, setup-captive state and AP DHCP are
  stopped, then APSTA changes to STA. The general HTTP server remains running
  and interface-independent.
- Setup and recovery use `bp10-xxxx`. Recovery uses the configured existing
  setup timeout. Fixed a second defect found during hardware testing: repeated
  failed reconnects used to restart the fallback deadline indefinitely.
- Added credential-free runtime diagnostics for Wi-Fi mode, STA state/IP,
  configured AP SSID, DHCP server, setup-captive state, and general HTTP
  independence.

## Hardware evidence

- Factory reset boot: `bp10-4018`, BSSID `24:EC:4A:01:40:19`, externally
  visible; serial runtime: `mode=APSTA`, `STA=disconnected`, `AP-SSID=bp10-4018`,
  `DHCP=running`, `setup-captive=running`. IDF logged DHCP server on
  `WIFI_AP_DEF` at `192.168.4.1`.
- The Linux test host could see the open AP but NetworkManager timed out before
  association (no `AP_STACONNECTED` event reached the ESP). Thus end-to-end
  HTTP through the setup AP could not be independently completed on that host;
  AP beacon, AP netif and DHCP start are directly evidenced.
- Valid saved network boot: IDF `mode: sta`; GOT_IP `192.168.178.122`; runtime
  `mode=STA`, `DHCP=stopped`, `setup-captive=stopped`, lifecycle
  `connected_sta_only`. `/api/status` remained reachable over home LAN.
- Fresh external scan after cache expiry showed neither `ESP_014019` nor
  `bp10-4018` during STA-only operation.
- Invalid test network: after the configured 180 s timeout, serial logged
  fallback expiry at 196.164 s, APSTA start, `bp10-4018`, DHCP running and
  setup-captive running. External scan confirmed `bp10-4018` at the expected
  BSSID.
- Sensitive NVS was backed up with owner-only permissions for the destructive
  tests, restored byte-for-byte, and the temporary backup deleted. Final device
  state is the original valid saved network, STA-only, LAN API reachable, with
  no setup SSID in a fresh scan.

## Verification

- ESP-IDF build: pass; `git diff --check`: pass.
- Flashed `/dev/ttyACM0` and booted successfully.
- Final firmware ELF SHA-256:
  `c272748a214a744a82b1af2bc0ade90808fec570420263c935029a46c9b3a54c`.
- Existing dirty DSP/UI files were preserved; implementation changes are
  limited to `main/wifi_manager.c` plus this ticket evidence.

## Follow-up: provisioning response race

Paul confirmed that phone association, setup UI, scan, network selection, and
password entry all worked after Factory Reset, but Save & Connect ended with
the browser message `Failed to fetch`.

- Root cause: `POST /api/wifi/config` called `wifi_manager_connect_sta()` before
  `send_ok()`. The STA channel transition and the intentional AP teardown after
  GOT_IP could therefore disconnect the setup client while its HTTP response
  was still in flight.
- Credentials are still persisted synchronously. The connection itself is now
  copied into manager-owned RAM and scheduled for the lifecycle task 1500 ms
  later, after the acknowledgement has left the handler/socket.
- The acknowledgement includes the device mDNS URL and transition delay. The
  frontend no longer issues an unnecessary config GET between save and status
  polling. Loss of `192.168.4.1` is treated as the expected setup-to-home-network
  transition and presents a clickable mDNS continuation instead of a red
  transport error.
- Hardware smoke test after flash: the POST returned complete HTTP 200 JSON in
  134 ms; serial logged scheduling at 1500 ms and only then entered the Wi-Fi
  connection function. The device remained reachable on its restored LAN IP.
- Final build/flash passed; `git diff --check` passed. Final ELF SHA-256:
  `87117c091a5e672ea1f7107af9745f0c94cf7c3cef7b904a005113a96df758a1`.
  After the final flash the controller returned to `connected_sta_only`, with
  the setup AP inactive and the LAN API reachable.
- Paul's phone is required for the final AP-origin browser confirmation because
  the Linux test host still cannot associate with this open ESP AP. Keep this
  ticket in review until that one confirmation is received.

## Final acceptance

- Paul completed the full phone workflow: Factory Reset, connection to the
  setup AP, WLAN scan, credential entry, Save & Connect, and transition to the
  home network all succeeded without the previous fetch failure.
- The home-network web UI remained reachable. The `bp10-4018` entry disappeared
  from the client WLAN list after its scan cache refreshed; the setup SoftAP was
  therefore confirmed inactive in the final STA-only state.
- Accepted by Paul on 2026-07-18. Ticket closed for release `0.3.6`.
