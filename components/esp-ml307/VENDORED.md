# Vendored copy of 78/esp-ml307

This directory is a source copy of the upstream `78/esp-ml307` component, kept in
tree instead of being pulled as a managed dependency.

- Upstream: https://github.com/78/esp-ml307
- Version: 3.7.5
- License: Apache-2.0 (see LICENSE)

## Why vendoring is still needed

`main/idf_component.yml` asks for `78/esp-ml307: ~3.7.3`, but the managed release
cannot build for the **ESP32-S2** target that AlphaPi One S uses:

```
CMake Error at .../78__uart-uhci/CMakeLists.txt:25 (message):
  uart-uhci: unable to locate hal/uhci_ll.h for esp32s2
```

The ESP32-S2 has no UHCI DMA controller. `78/uart-uhci` probes for
`hal/uhci_ll.h` for the selected target during configuration and calls
`message(FATAL_ERROR)` when it is missing, so the failure happens before a single
file is compiled.

Upstream triggers this because its rules are written as
`target not in [esp32]` — which includes the S2 — while `src/at_uart.cc` uses
`UartUhci` for every DMA transfer. The cellular modem path is simply not
available on the S2.

## What this copy changes

Only two files differ from the upstream 3.7.5 release:

1. **`CMakeLists.txt`** — the cellular modem sources (`at_uart.cc`,
   `at_modem.cc`, `ec801e/*`, `ml307/*`) are gated on
   `IDF_TARGET STREQUAL "esp32s3" OR IDF_TARGET STREQUAL "esp32c3"` instead of
   upstream's `NOT IDF_TARGET STREQUAL "esp32"`.

2. **`idf_component.yml`** — `78/uart-uhci` is restricted to the same two
   targets, so it is never resolved for the S2.

Everything else — including the `NetworkResult` / `std::expected` error-handling
layer that `main/ota.h`, the notification player and the MQTT/WebSocket
protocols depend on — is byte-for-byte upstream.

> The ESP32-S2 keeps the shared ESP network implementation
> (`src/esp/esp_*.cc`), `src/http_client.cc`, `src/web_socket.cc` and
> `src/network_error.cc`. It only loses the AT-command cellular modem.

## When updating

Replace this directory with the new upstream tag, re-apply the two edits above,
refresh the version field, then rebuild the AlphaPi One S target. Do **not**
simply raise the version number in `VENDORED.md` without re-checking the gate —
that is how this copy fell behind at 3.6.6 and broke the 2.5.1 upgrade with
`fatal error: network_error.h: No such file or directory`.
