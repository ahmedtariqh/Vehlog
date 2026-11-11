# ELM327 and CAN bench notes

VehLog has a working offline reader and a proposed design for a user-programmable vehicle logger. The sample inputs are synthetic. No adapter firmware or PCB has been identified, flashed, or tested on a vehicle.

## Run the reader

Use Python 3.11 or newer for the pilot. No package installation is required. On Windows, use `py -3` in place of `python3` if needed.

```sh
python3 capture_reader.py --source candump examples/candump.log
python3 capture_reader.py --source candump --format csv examples/candump.log > capture.csv
python3 capture_reader.py --source elm examples/elm-monitor.log
```

- The SocketCAN importer reads timestamped compact records from Linux `candump -L`. It preserves channel, timestamp as written, CAN ID width, payload, and CAN FD flags. It also distinguishes classical remote and SocketCAN error frames.
- The ELM importer expects a spaced Classical CAN monitor transcript with headers visible, CAN formatting and displayed DLC disabled, and an 11-bit or 29-bit ID. It accepts 29-bit IDs printed together or as four spaced bytes. It preserves line order. ELM text has no bus timestamps, so those fields stay absent in JSON and blank in CSV.
- Unknown records are preserved as `unparsed`. An explicit `BUFFER FULL` or compact-log `DROPCOUNT:` line becomes a `capture_overrun` record. Exit code 2 means one or more of those records occurred. Exit code 1 indicates an I/O error. A successful conversion establishes that the input format was understood. To evaluate capture completeness, inspect the device and host drop counters separately.
- This is an offline converter. It does not open a vehicle connection, decode OEM data, or replay traffic. Keep the original capture alongside any exported CSV or JSON.

## Characterize the exact ELM327 board

1. Record the label, USB/Bluetooth/Wi-Fi transport, PCB markings, both PCB faces, and readable chip markings. `ATI` is a reported version string. Identify the controller and transceivers from the board.
2. On a current-limited bench supply with the vehicle disconnected, identify power, ground, UART levels, and candidate programming pads using continuity checks and the component datasheets. Map each fitted transceiver to its diagnostic connector pins. Record the power path and whether any CAN termination is fitted.
3. Use the identified controller's documented debug interface to check access. Preserve the existing image and configuration if readable, and keep the original adapter available as a comparison device. An erase or unlock can destroy the factory image. If readback is protected or the chip cannot be identified, characterize serial behavior from the outside and move the custom firmware to documented replacement hardware.
4. On a known, terminated bench CAN network with an independent transmitter and another node providing ACKs, compare the ELM transcript with a reference capture. Check command support, formatting, frame counts, extended IDs, and behavior under load. Verify electrical silence rather than relying on a clone's command responses.

### Sample Classical CAN monitor configuration

For a bench network verified to use 500 kbps and 11-bit IDs, the ELM327 datasheet describes the following settings. Send one command at a time, check the response, and save the resulting plain terminal text. Start with the documented defaults on a dedicated bench session so stale CAN filters are not carried over. Use a protocol selection that matches the bench network. Avoid automatic protocol search during passive characterization.

```text
AT E0
AT L0
AT S1
AT H1
AT D0
AT CAF0
AT CFC0
AT CSM1
AT SP 6
AT CRA
AT MA
```

If the clone rejects a command or its output differs from the expected layout, retain the transcript and check its behavior on the bench before relying on it.

## Proposed logger hardware and firmware

Start with a dual-channel Classical CAN/CAN FD design. The firmware baseline selects ESP32-C5, which has two TWAI controllers. The first firmware uses one controller and one external automotive transceiver. See the [firmware bench guide](firmware/README.md). The TI TCAN1051 family has a hardware silent input, with the G/V suffix options covering up to 5 Mbit/s and a separate I/O supply where needed. Validate the exact part, pin allocation, clocks, and bit rates against the target bus. Default to controller listen-only plus hardware transmit inhibition through reset. Keep optional bench termination switched out on a vehicle bus. Include a fused and transient-protected input power stage, with reverse battery and load-dump protection, and verify the 12 V or 24 V operating envelope before vehicle use.

Capture at the device using one monotonic timebase across channels. Preserve channel, timestamp, raw frame, ID width, CAN FD flags, and error or drop events. Buffer bursts before storage, retain hardware and software drop counts, and validate that USB or SD writes can keep up. Add a separate configuration interface for bitrate, filters, and recording settings. Keep diagnostic requests explicitly enabled and separate from passive capture. Verify on the bench that the logger generates no ACKs, errors, or traffic in passive mode and that it stays silent during resets.

Test using a reference capture under sustained load on both channels. Compare frame counts and payloads, measure timestamp ordering and skew, provoke and account for drops, then test unplug and power-loss recovery. Record the logger firmware revision and all channel settings in each capture.

## Vehicle coverage

| Target | Next hardware or software requirement |
|---|---|
| CAN passenger cars and light vehicles | Record the bus the connection exposes. Add separately enabled standard OBD-II diagnostic polling and a vehicle-specific decoder when available. |
| Newer CAN FD platforms | Use an FD-capable controller and transceiver, correct nominal/data bit rates, and verified network access. |
| Older vehicles | Add K-Line for ISO 9141-2/ISO 14230-4 and J1850 VPW/PWM front ends where needed. They need distinct electrical interfaces and import support. |
| Heavy-duty vehicles | Add a verified connector harness, a suitable power stage, and J1939 decoding. Determine other legacy protocol needs vehicle by vehicle. |
| DoIP platforms | Add a suitable Ethernet interface, the appropriate vehicle connection, and a DoIP stack. |
| Proprietary signals and ECU-internal records | Use authorized OEM data and diagnostic access where available. A connection at the diagnostic port may expose only gateway-approved traffic. ECU-internal records may never appear on an accessible bus. |

Make coverage measurable per vehicle, ECU, and network. Record which buses were accessible, which frames were captured, which signals were decoded, and whether drop counts were measured. Preserve raw input alongside decoded output so new vehicle profiles can be applied later.

## References

- [ELM Electronics ELM327 datasheet](https://www.elmelectronics.com/wp-content/uploads/2017/01/ELM327DS.pdf)
- [Linux kernel SocketCAN documentation](https://docs.kernel.org/networking/can.html)
- [Linux CAN utilities](https://github.com/linux-can/can-utils)
- [ESP32-C5 TWAI documentation](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32c5/api-reference/peripherals/twai.html)
- [TI TCAN1051-Q1 CAN FD transceiver](https://www.ti.com/product/TCAN1051-Q1)
- [TI automotive battery input protection reference design](https://www.ti.com/tool/TIDA-01167)
- [CSS Electronics on vehicle gateways and UDS access](https://www.csselectronics.com/pages/uds-protocol-tutorial-unified-diagnostic-services)
- [CSS Electronics on heavy-duty J1939 logging](https://www.csselectronics.com/pages/obd2-data-logger-sd-memory-convert)
- [Vector on diagnostics over IP](https://certification.vector.com/mod/page/view.php?id=167)
