# Firmware architecture and vehicle coverage

## Implemented layers

| Layer | Code | Ownership |
|---|---|---|
| Protocol and scan | `components/vl_core/` | ISO C11 protocol, decoder, Mode 01 scan and bounded physical reads. Depends on frame types and callbacks for bus send/receive, monotonic clock, pacing and samples. |
| Serial record encoding | `components/vl_text/` | ISO C11 candump and diagnostic NDJSON formatting into caller-owned buffers. A platform decides where to write them. |
| FreeRTOS adapter | `components/vl_freertos_can/` | Static receive queue, ISR-to-task notifications, timeout and delay conversion. Uses standard FreeRTOS APIs. |
| ESP32-C5 hardware adapter | `components/vl_esp/` | TWAI configuration, GPIO, fixture arm, hardware timestamps, and conversion between driver frames and portable frames. |
| ESP32 application | `main/vl_main.c` | Kconfig profile selection, callbacks and console output. |
| Host build and tests | `portable/`, `tests/` | Builds the same C11 libraries without the SDK. Tests simulate the scanner clock and transport and verify the log encodings. |

The core has no SDK, RTOS or console headers. FreeRTOS remains the runtime on ESP32. The queue adapter uses the same FreeRTOS API on a different supported MCU or an upstream kernel build. ESP-IDF's FreeRTOS distribution has its own extensions and header layout. The CMake definition for that layout is private to the adapter. [Espressif FreeRTOS overview](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32c5/api-reference/system/freertos.html)

### Porting to another target

1. Reuse `vehlog_core` and, for the existing wire log formats, `vehlog_text`. Build or import the libraries through `portable/CMakeLists.txt`.
2. Implement the existing `vl_scan_port_t` callbacks in the new board's composition. The receive function reports frame, timeout or loss/error. Provide a wrapping monotonic millisecond clock, bounded waits, and an output sink. The scan still runs synchronously in one owner task.
3. For upstream FreeRTOS, configure the kernel and its port in the enclosing project first. Set `VEHLOG_BUILD_FREERTOS_ADAPTER=ON` and add the portable directory with `add_subdirectory`. Link `vehlog_freertos_can` and call its ISR functions from the board driver. Use the returned flag to request an ISR yield in that platform's driver. Enable static allocation, task notifications, `vTaskDelay` and `xTaskGetCurrentTaskHandle`.
4. The supplied queue adapter owns one channel and notification slot zero of its single bus owner task. Keep that task's notifications exclusive to this adapter. Initialize before enabling receive interrupts. Supply the transceiver and hardware driver, then qualify physical timings and protection on each target.
5. An application using another scheduler or a bare-metal main loop can implement the same callbacks if it can provide bounded synchronous I/O. Keep board configuration and future K-Line, J1850 and J1939 implementations outside the existing CAN scanner.

The receive ISR copies a bounded frame and signals faults. It performs no formatting or protocol decoding. The production task split below is a design milestone.

## Production task split

| Task | Owns | Bounded interface |
|---|---|---|
| Vehicle bus | Selected transceiver, configured protocol session, receive ring and deadline scheduler | Static frame/event queue to the decoder. Wake, stop and fault operations are local. |
| Sampler | GNSS, IMU, supply monitor, clock quality | Fixed-rate typed samples and monotonic capture timestamps. |
| Decoder and policy | Decoder registry, freshness and alert evidence | Profile-specific typed observations, per-signal polling budget, explicit unavailable states. |
| Storage | Append-only journal, stream cursors, record CRC and boot/session metadata | Separate reserved space for priority events and recorded loss ranges. |
| Uplink and update | Cellular transport, mutually authenticated ingestion, durable ACKs and signed updates | Fixed-size batch handoff. No raw vehicle transmit endpoint. |

Size queues and task stacks from measured worst-case rates, storage latency, ISR time and task high-water marks. Bus work has a bounded deadline and cannot wait for flash or the modem. Persist data before acknowledging it. When a queue fills, record where observation was lost. Carry capture and ingest times separately. Use watchdog supervision for task progress and explicit states for boot, idle, capture, uplink backoff and shutdown. Bind timestamps to a trusted clock source before using telemetry as rental evidence.

## Compatibility scope

[VEHLOG-ACQ-001](ELM327_PARITY.md) makes genuine ELM327 v2.3 data-acquisition parity a production release gate. K/L-Line, both J1850 variants, J1939, configurable CAN and all applicable read services are in the required baseline. The table below records implementation status.

“Supports a vehicle” means the verified adapter can discover the applicable diagnostic transport for that vehicle class and market, enumerate each reachable ECU's supported identifiers, and capture or decode those for which access and semantics are known. Regulated OBD exposes defined emissions or propulsion diagnostics. It does not give a generic scanner access to every physical sensor, internal ECU log or OEM network. An OBD socket, a CAN frame, a supported identifier and a verified value are different levels of coverage.

| Target | Firmware in this bundle | Coverage gate |
|---|---|---|
| SAE J1979 / ISO 15031-5 on ISO 15765-4 Classical CAN | Mode 01 discovery plus selected read-only services, manual bitrate, optional address byte and configurable flow control. Up to 4,095 bytes per physical read. | Validate the four CAN combinations, 250 and 500 kbit/s with 11- and 29-bit IDs. Add standards-driven discovery, test timing and service coverage, and vehicle/ECU fixtures. |
| CAN FD | Single-channel passive TWAI capture with a configured data bitrate. | Verify the transceiver and signal integrity. Implement and qualify the applicable FD diagnostic transport and addressing. |
| J1979 DTCs, readiness, freeze frame, information and test results | Native selected CAN reads and raw retention. Host DTC/VIN helpers and a configurable metric catalog. Legacy queries through a physical ELM327. | Add automatic per-service discovery and complete licensed definitions. Verify per-ECU response formats and fixtures. |
| SAE J1979-2 OBDonUDS and J1979-3 ZEVonUDS | Service implementations pending. | Support the applicable DoCAN or DoIP transport, standardized data identifiers and read-only service/session policy. Verify region and powertrain coverage. |
| ISO 9141-2 and ISO 14230-4 KWP | Available through the physical ELM327 host path. Native physical layer and protocol pending. | Include a protected K-Line interface and L-Line capability for platforms that require it. Verify initialization, timing and checksums. |
| SAE J1850 PWM and VPW | Physical layers and protocol pending. | Add the respective protected interfaces and validate timing with a reference tool and known ECUs. |
| Classical SAE J1939 | Host reassembly and DM1 decode from capture. PGN requests through physical ELM327. | Verify PGN requests, raw capture, DM1 and multi-packet receive on the appropriate harness. |
| OEM diagnostics | Profile-defined. Known authorized reads and raw reply retention are required for parity. | Verify the accessible network, gateway, ECU software and approved request before validating each decoder. |

Use a local, revisioned registry sourced from licensed standards and authorized OEM evidence. Keep service/identifier, ECU, transport, byte and bit ranges, scale, unit, precision, invalid-value conventions, timestamp and source. Retain raw data for undecoded identifiers when collection is authorized. Distinguish unsupported, blocked, timed out, stale, decoded and raw-only results. Poll only at an approved rate, with one outstanding operation per session and priority for the metrics actually used by rental rules. Preserve the checkout/return scope and telemetry disclosure from the existing Vehlog plan.

## Hardware qualification

- Start with a bench-only ESP32-C5 board, external high-speed automotive CAN FD transceiver and independent hardware transmit inhibit. Add K-Line/L-Line and J1850 electrical stages as separate validated circuits or removable front ends. A production configuration must fail silent during boot, reset, sleep, brownout, firmware update, and wrong-bitrate detection. Review the exact ESP32-C5 revision and each transceiver's electrical behavior.
- Use a protected 12 V input with fault isolation and current limiting. Define input and vehicle sleep-current budgets, connector wiring, transient, reverse battery, ESD and EMC requirements before PCB release. Validate a separate 24 V variant where required.
- Check zero transmitted dominant bits in passive mode with an independent analyzer. Exercise ACK, load and timing in the isolated lab profile. Compare capture frame counts and payloads against an independent logger under expected load. Stress malformed responses, lost fragments, simultaneous ECUs, clock discontinuities, reconnects and power cuts.
- Keep remote ingress away from the vehicle bus. Qualify secure boot, flash encryption, per-device identity, signed OTA with recovery/anti-rollback policy, and key lifecycle on a provisioned production image. Add threat analysis and verification records before field deployment.

## Standards baseline

- SDK and hardware. [ESP-IDF v6.1](https://github.com/espressif/esp-idf/releases/tag/v6.1) · [ESP32-C5 TWAI](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32c5/api-reference/peripherals/twai.html) · [ESP32-C5 errata](https://docs.espressif.com/projects/esp-chip-errata/en/latest/esp32c5/index.html) · [ESP32-C5 security](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32c5/security/security.html)
- Legislated diagnostic data. [ISO 15031-5:2015](https://www.iso.org/standard/66368.html) · [SAE J1979-DA July 2026, SAE supplements catalog](https://saemobilus.sae.org/standards/supplements) · [SAE OBDonUDS and ZEVonUDS catalog](https://saemobilus.sae.org/topics/electrical-electronics-and-avionics/on-board-diagnostics-obd)
- Transport and electrical interfaces. [ISO 15765-2:2024](https://www.iso.org/standard/84211.html) · [ISO 15765-4:2021](https://www.iso.org/standard/78384.html) · [ISO 9141-2](https://www.iso.org/standard/16738.html) · [ISO 14230-4](https://www.iso.org/standard/28826.html) · [SAE J1850](https://saemobilus.sae.org/standards/j1850_202212-class-b-data-communications-network-interface) · [ISO 13400-2:2025](https://www.iso.org/standard/87961.html)
- Qualification and cybersecurity. [SAE OBD compliance test catalog](https://saemobilus.sae.org/standards/recommended-practices) · [ISO 16750-2:2023](https://www.iso.org/standard/76119.html) · [ISO/SAE 21434:2021](https://www.iso.org/standard/70918.html)
