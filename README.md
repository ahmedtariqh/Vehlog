# Vehlog pilot

Vehlog is a proposed car rental platform with a tracker you control. This bundle contains an offline rental replay, an ELM327 and SocketCAN capture reader, a tracker log contract, and a build plan for the embedded product and private software.

## Open the demo

Open `demo.html` in a browser. It opens at the end of a synthetic rental. Click **Play** to start at checkout or move the slider to inspect any point. Between 07:00:55 and 07:01:00 UTC the tracker is stale. At 07:01:06 UTC the rental becomes overdue. At 07:01:20 UTC it checks in. The export embeds its in-window location data, so treat exports from real trips as sensitive files.

## Run the log reader

Use Python 3.11 or later. On Windows, replace `python3` with `py -3`. Run from the extracted directory.

```sh
python3 vehlog.py replay examples/tracker.ndjson examples/rental.json --html rental-replay.html --json rental-events.json
python3 vehlog.py inspect-capture examples/bench-can.log
python3 vehlog.py inspect-capture --source elm examples/elm-monitor.log
python3 capture_reader.py --source candump --format csv examples/bench-can.log > capture.csv
python3 -m unittest discover -s tests
```

Change `examples/rental.json` to configure the speed limit, tolerance, dwell time, distance allowance, circular rental area, and return deadline. Run the first command again to apply the changes. The ELM example intentionally produces exit code 2 because it contains an adapter overrun. The inventory includes counts by channel, format and ID, plus a SHA-256 of the original input. The converter also exports raw payloads. See [ELM327 bench notes](ELM327_Bench_Notes.md) for the legacy formats.

## Tracker log format

The example device emits newline-delimited JSON. Each record represents one observation for a single assigned tracker. The replay accepts timezone-aware ISO 8601 timestamps and sorts the input by capture time. A repeated device, boot and sequence with the same normalized content is a retransmission. Conflicting duplicates and conflicting timestamps are rejected. The pilot accepts up to 100,000 rows, each at most 8 KiB.

```json
{"v":1,"device":"tracker-demo","boot":"bench-1","seq":12,"time":"2026-11-09T09:00:44+02:00","fix":{"lat":30.15458,"lon":31.6543,"accuracy_m":8},"speed_kph":40,"power":"vehicle"}
```

| Field | Definition |
|---|---|
| `v` | Protocol version, currently 1. |
| `device`, `boot` | Device ID and unique ID for the current boot. |
| `seq` | Unsigned 32-bit sequence incremented for every emitted sample. Start a new boot namespace before wraparound. |
| `time` | Capture time with explicit offset. An offline replay assumes the device clock was trustworthy. |
| `fix` | Latitude and longitude in decimal degrees, estimated horizontal accuracy in metres, or null. |
| `speed_kph` | GNSS speed in km/h or null. Requires a fix when supplied. |
| `power` | Vehicle supply, backup supply, or null when the hardware cannot report it. Backup power is evidence of a supply transition only. |

The replay uses the checked-out interval from `start` to the actual check-in `returned` (exclusive). If no check-in is supplied, it uses `observed_until` (inclusive). A rule can only use evidence from that interval. GPS fixes reported worse than 30 m are withheld from the route and GNSS rules. A reboot, missing sequence or interval longer than 10 seconds breaks distance and dwell continuity. The geofence rule accounts for the reported accuracy on both sides of the boundary. The route view plots the most recent 6,000 observations and the latest 100 events. JSON export retains the full replay. Distance is an estimate and gaps are omitted. Raw CAN imports are separate from the GNSS rental replay and require a validated decoder before they can supply rental signals.

## ESP32 firmware

[ELM327 v2.3 data-acquisition parity](firmware/docs/ELM327_PARITY.md) is a mandatory product release requirement. The research report and test matrix specify the protocol, diagnostic service, raw traffic and adapter data coverage that must be proven.

The [ESP32-C5 firmware](firmware/README.md) adds a layered ESP-IDF and FreeRTOS baseline, a passive CAN/CAN FD capture profile, and a separate read-only Mode 01 lab scanner. The firmware docs cover the legacy and UDS work required for broad OBD-II coverage and the MISRA C:2025 compliance evidence still required.

Run `python3 vehlog.py inspect-diagnostics scan.ndjson` on one clean firmware console scan to group readings and raw payloads by ECU. Exit status 2 identifies a missing, failed, partial or unconfirmed scan end.

## Build the embedded product and rental service

Read the [build plan](VEHLOG_BUILD_PLAN.md) for the hardware split, RE evidence ledger, vehicle coverage, booking workflow, security design and pilot acceptance checks. The code and demo use synthetic data. Hardware, live cellular connectivity, bookings, payouts, push delivery and OEM decoding require implementation and validation.

## Diagnostic acquisition tools

```sh
python3 vehlog.py inspect-diagnostics scan.ndjson --catalog examples/pid-catalog.json
python3 vehlog.py inspect-j1939 truck-can.log
```

The sample PID catalog demonstrates the schema. A catalog's offset starts at byte zero of the reassembled PDU, including the positive response service byte and selector. Supported fields include byte order, signed integers, unsigned masks and shifts, rational scale and bias, and unavailable raw encodings. Results include the catalog revision and exact rational values. Load an applicable licensed or independently verified definition set to expand the catalog.

The optional physical-adapter CLI uses pyserial. Install it into your Python environment and supply the serial port and baud rate for a genuine ELM327 on an authorized test setup. Use one adapter session at a time. The bridge preserves command replies as text and as base64 bytes up to the adapter prompt.

```sh
python3 -m pip install pyserial
python3 vehlog.py elm /dev/ttyUSB0 --protocol 0 --request 0100 --request 0902 > obd.ndjson
python3 vehlog.py elm /dev/ttyUSB0 --protocol 6 --duration 5 > can-monitor.ndjson
python3 vehlog.py elm /dev/ttyUSB0 --protocol A --pgn 00FECA > j1939-request.ndjson
```

Protocol slots 1–9 and A–C use the hardware and protocol support of the attached interpreter. Passive monitoring requires an explicit protocol. For protocol B, a lab session can use `--bench --user1` to set temporary options and bitrate divisor. Protocol C uses the adapter's stored configuration. Bench-only options can select verified CAN headers and ISO-TP address bytes. OEM Mode 22 reads require explicit transmit and response IDs. Code clearing and actuator requests are rejected. The adapter reports the ignition input's state as received, which is meaningful only when that input is wired. Serial and vehicle qualification remains part of the [ELM327 parity gate](firmware/docs/ELM327_PARITY.md).
