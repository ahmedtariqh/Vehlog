# Vehlog ESP32-C5 firmware

The firmware targets the ESP32-C5 and ESP-IDF v6.1, with ESP-IDF FreeRTOS. Project-owned protocol and scan code uses C11 and has a separate host test build. The coding target is MISRA C:2025. See the [compliance plan](docs/MISRA.md) for the release evidence still required and the [architecture and coverage](docs/ARCHITECTURE.md) for the complete vehicle support target.

The [ELM327 parity research and release gate](docs/ELM327_PARITY.md) governs production diagnostic and capture coverage. This firmware remains the bench baseline while those cases are open.

## Build and test

```sh
make test
```

The SDK-independent libraries and tests are also available through CMake 3.22 or newer.

```sh
cmake -S portable -B build-host
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

For multi-configuration generators, pass `--config Release` to the build and `-C Release` to CTest. Other projects can add the portable directory and link `vehlog_core` or `vehlog_text`. A project that provides upstream `freertos_kernel` can also enable the [FreeRTOS adapter](docs/ARCHITECTURE.md#porting-to-another-target).

Use Espressif's [v6.1 release](https://github.com/espressif/esp-idf/releases/tag/v6.1) and its ESP32-C5 toolchain. Activate that SDK environment before running the following commands in this directory.

```sh
idf.py set-target esp32c5
idf.py menuconfig
idf.py build
```

In **Vehlog**, select the TX and RX pins verified for your specific board and external transceiver. The defaults leave both pins unset, so the driver stays stopped. Verify the board pinout, including strapping, flash, USB and debug pin conflicts. Configure the nominal bitrate for the isolated test bus. Choose a data-phase bitrate only for a validated CAN FD setup.

### Passive capture

Leave **Enable one-shot active scan** off. The firmware has one listen-only TWAI instance and a static 48-frame RX queue. Use an external automotive CAN transceiver with the transmitter physically disabled and the receiver enabled. Test on an isolated CAN fixture with an independent transmitter and ACK-capable receiver. Verify that the capture unit emits no dominant bits through boot, reset and brownout before any vehicle connection.

The serial console emits compact candump-compatible records with monotonic boot-relative timestamps. Capture clean application lines beginning with `(` and all `DROPCOUNT:` lines to a file. Keep boot boundaries separate because the timebase resets. The reader in the parent project consumes this file with `python3 vehlog.py inspect-capture capture.log` or exports individual records with `capture_reader.py`. A `DROPCOUNT: unknown` marker means the capture has incomplete evidence. UART output is a bench convenience and can bottleneck a busy bus.

### Active bench scan

Use a separate SDK configuration with **Enable one-shot active scan** on, verified GPIOs, and an isolated ECU simulator. Use an independent physical transceiver gate that defaults to silent and an active-low arm sense GPIO with an external pull-up. The GPIO input is a software check. The external gate is the actual transmit interlock. Configure either 11-bit or 29-bit normal addressing and a verified Classical CAN bitrate. The active build performs one scan when started armed and disables the controller when the scan ends. Keep the active build off vehicles pending electrical, protocol and hardware-in-the-loop qualification.

The scanner sends a J1979 Mode 01 supported-PID request, tracks each responding ECU separately, follows advertised bitmap pages and queries each advertised data PID once. Requests are paced and restricted to the scanner's built-in read-only sequence and ISO-TP flow control. It preserves undecoded PDUs and distinguishes observed data, missing responses, negative responses and incomplete transfers. It decodes RPM, speed, coolant temperature and fuel level when their reply lengths match. The lab implementation uses one outstanding request at a time, a 16-ECU discovery bound, and a 4,095-byte Classical CAN ISO-TP receive limit. It assumes no competing diagnostic tester on the fixture. The initial response budget is 400 ms. A matching response-pending indication gives that physical request a 5 s stage budget. Valid segmented progress gives it a 1 s stage budget. The absolute bound is 30 s per read.

To read a specific response on the same fixture, enable **Perform one configured physical OBD read instead of PID discovery**. Set the physical request and response IDs, service, and the applicable selector and freeze-frame number. Services 01, 02, 03, 06, 07, 09 and 0A are accepted. Use the optional ISO-TP address-byte fields only for verified addressing on the fixture. Flow-control block size and STmin are configurable. The single-read profile emits one version 1 NDJSON result with a service field. Service 02 includes its freeze-frame number.

The serial log uses version 1 diagnostic NDJSON. `status` is `supported_page`, `data`, `negative`, `no_response`, or `incomplete`. `pdu` contains the hexadecimal diagnostic payload, starting with the response service byte, when captured. Initial discovery pages carry both the bitmap and its original PDU. `value_milli` is meaningful only when `unit` is nonempty and is divided by 1,000 for display. `mono_us=0` can indicate absence of a response timestamp. `scan_end` reports a status code from `vl_scan_status_t` and whether driver disable succeeded. From the parent directory, use `python3 vehlog.py inspect-diagnostics scan.ndjson` to read one clean scan and group its records by ECU. The host parser can extract supported CAN DTC and VIN encodings and accept a revisioned `--catalog` for additional field decoding. Other valid replies remain available as raw PDUs. A successful scan covers the responders and data advertised under its configured service and bus mode. It is a snapshot of one diagnostic session.

## Verification baseline

The passive, Mode 01 and extended-address single-read firmware profiles compiled and linked for ESP32-C5 with ESP-IDF v6.1 at commit `fff9895c82d744c7237be8847347bdd1b07c6643` and Espressif's RISC-V GCC 15.2.0 toolchain. The compile database confirms ISO C11 for the core and text encoder and GNU C11 for the SDK-facing components. The passive ELF has no linked Vehlog scan or TWAI transmit function. Host fixtures passed strict compiler warnings, GCC's static analyzer and UBSan. During the preceding portability verification, the same FreeRTOS adapter was built and run with upstream FreeRTOS 11.2.0 on its Linux POSIX port at a 100 Hz tick, exercising its static queue and sub-tick wait conversion. The firmware has not been flashed or validated on an ECU simulator or a vehicle.

## Data integration

Raw CAN and diagnostic NDJSON are engineering streams. The existing rental replay consumes a separate tracker schema with trusted UTC and GNSS measurements. Field firmware still needs GNSS, a trusted time mapping, the durable upload queue and authenticated cellular ingestion. Any future ECU-derived metric must carry its ECU, protocol, profile, unit, capture timestamp and validity through to the app.
