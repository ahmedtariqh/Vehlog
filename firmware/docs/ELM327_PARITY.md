# ELM327 data acquisition parity

**VEHLOG-ACQ-001 is a mandatory release gate.** With the same accessible bus, harness, vehicle or fixture state, authorized diagnostic session and request configuration, Vehlog must preserve every in-scope data response and observable message that a genuine ELM327 v2.3 can acquire. It must also expose equivalent local adapter measurements. The [acceptance matrix](ELM327_PARITY_MATRIX.csv) defines the minimum case inventory. All cases need evidence for the exact hardware and firmware revision before a parity claim.

## What is missing today

| Area | What the current pilot does | Open work |
|---|---|---|
| Physical buses and discovery | Native CAN with configured bitrate and IDs. Host ELM path selects the genuine interpreter's protocol or auto selection for active reads. | Native K/L-Line and J1850 front ends, legacy initialization, native automatic detection and configurable USER profiles. Verify each supported adapter's populated circuitry. |
| CAN diagnostic transport | 4,095-byte Classical ISO-TP receive, normal/extended addressing, configurable flow control and per-request pending/progress timers. | Qualify on an actual ECU fixture and under load. Concurrent multi-ECU segmented transactions and broader filter profiles remain open. |
| Diagnostic data | Mode 01 discovery. Selected native CAN reads for 02, 03, 06, 07, 09 and 0A. Host ELM reads include legacy Mode 05 and bench-only supported-control discovery. | Automatic discovery across services, a complete licensed registry, and validation against each target OEM/profile. |
| J1939 | Host PGN requests through the ELM. Offline CAN capture decoder with source-specific transport assembly and DM1. | Native active J1939 transport and production qualification. |
| Capture and adapter data | Native CAN/CAN FD logging. Configured host ELM passive monitoring and voltage, ignition input, activity and CAN-status queries. | Native voltage and ignition measurement/calibration, legacy monitors and qualified throughput/loss metrics. |
| Existing ELM327 clients | Vehlog CLI can control a genuine ELM327. | Native drop-in AT interface and compatibility testing with third-party clients. |

The [acceptance matrix](ELM327_PARITY_MATRIX.csv) tracks the remaining validation. The rental device excludes state-changing controls from its acquisition policy. CAN FD, newer UDS profiles and DoIP remain separate from legacy ELM327 data parity. [E1] [S1] [S5] [S9] [S12]

## Reference and scope

Use a genuine ELM327 v2.3 on a fully populated adapter and an independent bus analyzer. Elm lists the low-voltage ELM327L v2.3 as functionally equivalent. Record PCB and chip markings, host transport and baud rate, populated front ends, firmware banner, programmable parameters and command settings. Confirm those settings by behavior. A version string alone does not establish authenticity or installed circuitry. The SHA-256 of the reviewed Elm datasheet is **40a50ed13129c611e3b54c16343e93a8a07ff757c6fe1c491675643ab61ea342**. This review used documentation and the Vehlog source. Physical reference measurements remain open. [E1] [E2]

| Part | Required outcome |
|---|---|
| Standard diagnostics | Acquire every supported read service and advertised data identifier for every reachable responder on the applicable transport. Keep original diagnostic payloads and decode standardized values against licensed definitions. |
| Raw and enhanced diagnostics | Preserve all frames the reference can see within the verified capture envelope. For authorized, known OEM or user-CAN reads, match addressing, framing, timing and raw replies. An unknown identifier or missing decoder must never cause silent loss. |
| Adapter-side data | Match input voltage, ignition sense when wired, protocol and bus activity/status, and error or capture-loss evidence. Distinguish connector voltage from an ECU-reported voltage. |

Data parity covers reads, applicable non-actuating capability discovery and passive observation. Clearing codes, commanding a test or component, coding, programming and security changes are separate operations. The rental device blocks them and has no remote raw transmit endpoint. Passively capturing their traffic in an authorized fixture is in scope. An exact third-party ELM terminal emulator is a separate compatibility surface. The controls below are required wherever they affect acquisition. [E1] [S1] [S3] [S13]

## Transport coverage

The ELM has twelve numbered or lettered protocol slots. Slot 0 selects automatic search. Slots 1–9 cover first-generation OBD transport choices. J1939 and the two user CAN profiles are separate parity requirements even if the first fleet uses passenger cars. The pin numbers are for the conventional J1962 application in Elm's schematic. Verify the actual harness and electrical profile. [E1, pp. 23, 38–39, 58–61, 74–77, 84] [S5]–[S9]

| ELM slot | Reference transport | Vehlog coverage required |
|---|---|---|
| 0 | Automatic protocol selection | Validate successful and unsuccessful discovery, correct lock, retry and fallback. Separate active discovery from passive capture. |
| 1 | SAE J1850 PWM, 41.6 kbit/s | Protected interface for pins 2 and 10, edge timing, framing, CRC, arbitration and applicable in-frame response behavior. |
| 2 | SAE J1850 VPW, 10.4 kbit/s | Protected single-wire interface on pin 2, pulse encode/decode, framing, CRC and applicable in-frame responses. |
| 3 | ISO 9141-2, 5-baud initialization, 10.4 kbit/s | Protected K-Line on pin 7, L-Line on pin 15 where required, slow initiation, keyword exchange, framing and timing. |
| 4 | ISO 14230-4 KWP, 5-baud initialization, 10.4 kbit/s | Verify KWP-specific handshake, headers, checksum, session timing and keepalive separately. |
| 5 | ISO 14230-4 KWP, fast initialization, 10.4 kbit/s | Verify fast-init waveform, start-session responses, delayed replies and clean closure. |
| 6 | ISO 15765-4 CAN, 11-bit ID, 500 kbit/s | External automotive CAN transceiver on pins 6 and 14, TWAI and ISO-TP. Verify functional and physical addressing. |
| 7 | ISO 15765-4 CAN, 29-bit ID, 500 kbit/s | Verify independent 29-bit functional and physical routing. |
| 8 | ISO 15765-4 CAN, 11-bit ID, 250 kbit/s | Exercise the complete acquisition path at this bitrate. |
| 9 | ISO 15765-4 CAN, 29-bit ID, 250 kbit/s | Exercise the complete acquisition path at this bitrate and ID width. |
| A | Classical SAE J1939, 29-bit ID, 250 kbit/s default | PGN, source and destination, global and directed reads, DM1, and multi-packet receive. Verify connector and supply profile. |
| B | USER1 Classical CAN, 11-bit ID and 125 kbit/s defaults | Configurable bitrate, ID width, masks and framing options. Test an approved alternate J1939 configuration such as 500 kbit/s. |
| C | USER2 Classical CAN, 11-bit ID and 50 kbit/s defaults | Independently configurable second user profile. Verify defaults and persistence where they affect acquisition. |

**Hardware consequence.** Include protected CAN, K/L-Line and both J1850 front ends in the board or a qualified interface module. ESP32-C5 TWAI provides the CAN controller and needs an external transceiver. ST's L9637 is a K-Line candidate subject to logic levels, protection and separate L-Line requirements. Use a timing peripheral or qualified controller for J1850, then verify its timing under RTOS load. Qualify electrical silence, transients, cranking, sleep current and EMC on the actual PCB. Elm announced its closure, and OBD Solutions lists the STN2120 as discontinued. Keep an authenticated ELM as the reference, with supply-risk review for any production interpreter. [E1] [E2] [S6]–[S10] [S14]

CAN FD, OBDonUDS, ZEVonUDS and DoIP remain separate milestones. ESP32-C5 supports CAN FD, while the ELM327 v2.3 reference CAN modes are Classical CAN. A newer vehicle requiring J1979-2 or J1979-3 needs the corresponding services and applicable transport validated independently. [E1] [S10] [S12]

## Diagnostic services and identifiers

All identifiers below are hexadecimal. Retain service-specific selectors. A PID, freeze-frame number, test or monitor identifier, vehicle-information type, UDS DID and J1939 PGN/SPN are separate namespaces. [S1]–[S4] [S11]

| Service | Data that must be acquired where supported | Validation point |
|---|---|---|
| 01 | Current data, per-ECU PID support bitmaps, MIL, readiness, sensors and structured metrics | Decode every standardized supported identifier. Keep raw bytes independently. |
| 02 | Freeze-frame data, trigger code and captured values | Preserve freeze-frame number and distinguish missing snapshots from empty or zero values. |
| 03 | Stored or confirmed emissions-related DTCs | Preserve responder, raw code bytes and decoded code. |
| 04 | Clear/reset diagnostic data | State-changing. The rental policy must block it. |
| 05 | Legacy oxygen-sensor monitor results where applicable | Test the non-CAN selectors and variants. Handle the applicable CAN monitoring path separately. |
| 06 | On-board monitor results, test values, limits and identifiers | Test applicable CAN and legacy formats, including signedness, scaling and multiple records. |
| 07 | Pending emissions-related DTCs | Preserve pending separately from stored and permanent DTCs. |
| 08 | Control-capability enumeration where a verified non-actuating discovery request is defined | Verify discovery on the fixture and block control execution. |
| 09 | VIN, calibration IDs, CVNs, ECU names and in-use performance counters where exposed | Test multiple records, ECU origins, count/framing rules and segmentation. |
| 0A | Permanent emissions-related DTCs | Preserve as a distinct category. A scan-tool reset does not clear them. |

A known OEM request can retrieve extra data through the same interpreter if the correct network, ECU and authorized session expose it. Individual TPMS values, ABS and SRS details and many EV battery readings require verified access and profile definitions. Compare OEM recipes on the same vehicle and session, and distinguish a denied, unavailable or unvalidated profile from a successful raw read. [S1] [S12] [S13]

### Discovery and decoding contract

1. Establish transport and addressing. Enumerate all responders using the applicable functional request where supported. Use individual physical requests where appropriate to keep subsequent acquisition unambiguous.
2. Start Mode 01 discovery at PID 00. Its big-endian 32-bit mask maps the most significant bit to the first identifier after the base and the least significant bit to the continuation page. Follow pages separately per ECU. Use the service-specific discovery methods for freeze frames, monitor results and vehicle information.
3. Track advertised, observed, decoded and validated separately. An advertised identifier that times out remains supported but unavailable for that observation. An unknown identifier remains raw. On CAN, verify multi-PID requests and reordered responses, then split concatenated values only when lengths and selectors are known.
4. Preserve original link frames and assembled PDUs separately. Record protocol, bus, ECU, addressing mode, request ID, service, selector, record or frame number, capture time and clock quality, transport outcome and decoder revision. Keep original padding and DLC in the frame record. Distinguish negative, pending, no response, denied, malformed and truncated data.
5. Build a versioned registry from licensed SAE J1979-DA, J2012-DA and J1939DA, with independently validated OEM definitions. Capture wire size, offsets, byte order, masks, signedness, scale, units, unavailable encodings, bounds, fixtures and source revision. Verify licensing and permitted redistribution. Preserve exact raw values for reprocessing.

Worked examples for fixture design. A and B are bytes after the response service and PID. [E1, pp. 32–37, 44–47] [S1] [S2] [S4] [S15]

| Mode 01 PID | Interpretation | Current pilot |
|---|---|---|
| 00 | First supported-PID bitmap | Partial per-ECU discovery |
| 01 | MIL, DTC count and monitor state | Raw if acquired |
| 05 | Coolant, A minus 40 °C | Scaled |
| 0C | Engine speed, (256A + B) / 4 rpm | Scaled |
| 0D | ECU-reported speed, A km/h | Scaled |
| 2F | Fuel level input, 100A / 255 percent | Scaled |
| 42 | Control-module voltage, (256A + B) / 1,000 V | Raw if acquired. Adapter-side voltage is independent. |

For example, in an assembled response the bytes 41 0C 1F 40 represent 2,000 rpm, while 41 0D 58 represents 88 km/h. A CAN frame's PCI and padding are not part of these assembled responses. Keep two copies of the same PID if two different ECUs return it.

Validate readiness bitfields, oxygen and equivalence-ratio values, fuel trims, air pressure and flow, fuel system status, aftertreatment data and composite identifiers. Exercise all valid lengths, sentinel values and min/max encodings before claiming the registry is complete.

## ELM controls that affect what data can be acquired

Use independent wire traces because terminal presentation can hide headers, PCI, padding or other fields. The examples below identify documented interface functions. Vehlog may implement the same data behavior through a structured interface. [E1, pp. 8–31, 44–66, 73–79, 91–93]

| ELM examples | Required Vehlog behavior |
|---|---|
| SP, TP, SS, DP, DPN, IA, PC | Manual and automatic protocol selection, lock and fallback, current protocol and active state, clean closure. |
| SH, CP, TA, AR, RA/SR, CEA/CER | Approved functional and physical headers, priority, tester/source and response address. A 29-bit CAN ID differs from ISO-TP extended addressing, which uses a payload byte as an address. |
| MA, MR, MT, FT, CRA, CF/CM, CSM | Monitor all accessible traffic and filter by source, destination or ID/mask as applicable. Verify electrical silence, including CAN ACK and error signalling, J1850 in-frame responses and legacy wakeups. Automatic protocol search can transmit and is a separate operation. |
| H, D, CAF, V, AL/NL, RTR | Preserve observable headers, address width, DLC, payload, PCI, padding and remote-frame metadata. Keep raw frames and assembled bytes separately. ELM output does not expose CAN CRC or J1850 in-frame response bytes. Use an independent analyzer for those signals when needed. |
| CFC, FC SH, FC SD, FC SM | Standard and approved custom flow-control addressing and bytes, with block size and separation time. Validate segmented replies, interleaved responders and rejection of incomplete transfers. |
| AT0/1/2, ST, CTM, JTM, response-count suffix, pending-response settings | Protocol-appropriate timing, complete response counts and extended waits. Maintain independent pending and timeout states. A positive reply from a second ECU must not end the first ECU's pending transaction. |
| SI, FI, IIA, IB, KW, SW, WM, IFR | Applicable slow and fast initiation, alternate legacy settings, keywords, keepalives, checksum and J1850 acknowledgement behavior. |
| MP, DM1, JE/JS, JHF, PB and USER parameters | PGN, source/destination, priority and full PDU. Preserve byte order on the wire, broadcast versus requested messages, and complete-message counting. Elm documents fixed tester addressing rather than J1939-81 address negotiation. |
| RV/CV, IGN, CS, AMC, BD and error indications | Adapter-side input voltage and calibration, ignition input when wired, bus state and activity, last-frame evidence and explicit error/loss markers. A structured snapshot can replace a textual buffer dump. |
| Host baud, prompt, echo, line spacing, reset and programmable settings | Preserve settings that affect the acquisition envelope. A byte-for-byte terminal emulator and arbitrary adapter EEPROM storage are separate interoperability concerns. |

**Receive length and timing.** The native Classical CAN bench path now accepts up to 4,095 bytes using caller-owned workspace storage, with per-read pending/progress deadlines and configurable flow control. Host fixtures exercise 4,095-byte reassembly, flow-control blocks, sequence and clock wrapping, two pending responses, and a 30 s absolute bound. Verify against a physical reference under load. The 48-frame ISR queue remains bounded, and parallel multi-ECU segmented acquisition is still a release item. [E1, pp. 46–47, 62–66] [S5]

**Classical J1939 has its own transport and registry.** Its transport format can represent 1,785 bytes per multi-packet message. Use that as a test boundary and measure the actual reference adapter's ability separately. Verify PGN requests and broadcasts, DM1, source and destination, multiple sources, and applicable broadcast and directed transfers. Keep incomplete transfers explicit. Do not route J1939 through the Mode 01 ISO-TP assembler. [E1, pp. 56–61] [S9]

## Build order and acceptance

| Workstream | Testable outcome |
|---|---|
| Reference and fixtures | Authentic reference and independent analyzer, deterministic responders, identified settings, repeatable initial state and versioned stimulus files. |
| Shared acquisition contract | Versioned raw frame, reassembled reply and decoded records. Unknown data, rejected frames and loss are visible. Rental telemetry is separate from engineering captures. |
| CAN and ISO-TP | Protocol discovery, all four mandated CAN variants, configurable user profiles, standard and approved custom addressing, segmented reads and timing. |
| J1979 and registries | All applicable read services, licensed revision-pinned definitions, independent expected values, decoder and raw-retention regression tests. |
| Legacy physical layers | Qualified K/L-Line, PWM and VPW circuits, with initialization, timing, capture and fail-silent tests. |
| J1939 and OEM | PGN capture and diagnostics, transport and approved OEM reads, with coverage per model year, region, ECU and harness. |
| Release | Pass the matrix on the exact production revision, validate against applicable scan-tool tests, and close hardware, cybersecurity and MISRA gates. |

For each case, run the ELM and Vehlog separately on the same deterministic fixture and reset it to the same initial state. Run passive comparisons against a repeatable bus trace with independent ACK where needed. Retain the original ELM transcript, independent bus trace and Vehlog capture with SHA-256 values, hardware revision, firmware identity, settings, timestamps and fixture ID. Compare raw frames by physical bus, direction, ID or header and bytes. Compare assembled replies by request, responder and service-specific selector. Where ordering among independent replies is immaterial, compare multisets that retain duplicate occurrences. For passive streams check order, frame counts and timestamp bounds. Validate scaled values against independently derived fixtures with explicit tolerance. Host-side ASCII timestamps are not bus timestamps. [E1] [S16]

Use the matrix's baseline-status column for implementation triage. Set an acceptance status to Verified only after recording the test envelope and linking the fixture manifest and evidence. Missing and partial baseline features are implementation work. Pending rows need qualification evidence. A row with no positive test stays Open.

**Pass criteria.** Zero missing or altered in-scope frames or replies in the documented load and timing envelope, explicit loss outside it, and zero unintended bus actions. Compare to an independent analyzer when the ELM overflows or its formatter suppresses detail. Require positive, empty, pending, negative, timeout, malformed, interrupted, buffer-full and wrong-bitrate scenarios. A vehicle that lacks a service provides a useful negative case. The release also requires a positive fixture for each in-scope service. Use SAE J1699/2 for scan-tool testing and applicable J1699/3 or J1699/5 and Alliance software for complementary vehicle-side validation. [E1] [S16]

**Current evidence.** The host fixtures exercise the portable C11 CAN path and Python decoders and a mocked serial adapter. The Python ELM path depends on the attached adapter for all electrical and protocol behavior. The offline J1939 decoder uses observed Classical CAN frames. The matrix remains Open pending genuine ELM327 A/B results and production-board qualification. The source tree does not include a licensed complete PID/PGN database.

## Sources

- **E1** · [Elm Electronics ELM327 v2.3 datasheet](https://www.elmelectronics.com/wp-content/uploads/2020/05/ELM327DSL.pdf). Command summary pp. 10–12, OBD/CAN pp. 32–48, J1939 pp. 56–61, custom CAN pp. 62–66, power and programmable settings pp. 67–77, capture throughput pp. 78–79, schematic p. 84, and errors pp. 91–93.
- **E2** · [Elm Electronics product and version comparison](https://www.elmelectronics.com/products/ics/obd/) · [Elm closure notice](https://www.elmelectronics.com/?p=58).
- **S1** · [SAE J1979 reaffirmed May 2025](https://saemobilus.sae.org/standards/j1979_202505-e-e-diagnostic-test-modes) · [ISO 15031-5](https://www.iso.org/standard/66368.html).
- **S2** · [SAE supplements catalog with July 2026 J1979-DA and J2012-DA](https://saemobilus.sae.org/standards/supplements) · [SAE J1939DA revision history](https://saemobilus.sae.org/standards/j1939da_201611-j1939-digital-annex). Verify the latest applicable annex at procurement and pin the licensed edition.
- **S3** · [SAE J1978-1 April 2026](https://saemobilus.sae.org/standards/j1978-1_202604-obd-ii-scan-tool-first-generation-protocols) · [SAE J1978-2](https://saemobilus.sae.org/standards/j1978-2_202512-obd-ii-scan-tool-second-generation-protocols).
- **S4** · [California BAR OBD test reference](https://bar.ca.gov/obd-test-reference) · [Permanent DTC explanation](https://bar.ca.gov/arsc/newsletters/newsletter/fall-2019/permanent-diagnostic-trouble-codes).
- **S5** · [ISO 15765-2:2024](https://www.iso.org/standard/84211.html) · [ISO 15765-4:2021](https://www.iso.org/standard/78384.html) · [Linux ISO-TP](https://docs.kernel.org/networking/iso15765-2.html).
- **S6** · [SAE J1962](https://saemobilus.sae.org/standards/j1962_201607-diagnostic-connector) · [ISO 16750-2 electrical loads](https://www.iso.org/standard/76119.html).
- **S7** · [SAE J1850](https://saemobilus.sae.org/standards/j1850_202212-class-b-data-communications-network-interface).
- **S8** · [ISO 9141-2](https://www.iso.org/standard/16738.html) · [ISO 14230-4](https://www.iso.org/standard/28826.html) · [ST L9637](https://www.st.com/en/automotive-analog-and-power/l9637.html).
- **S9** · [SAE J1939-21](https://saemobilus.sae.org/standards/j193921_202205-data-link-layer) · [SAE J1939-73 September 2026](https://saemobilus.sae.org/standards/j1939-73_202609-application-layer-diagnostics) · [Linux J1939](https://docs.kernel.org/networking/j1939.html).
- **S10** · [ESP32-C5 TWAI, ESP-IDF v6.1](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32c5/api-reference/peripherals/twai.html).
- **S11** · [SAE J1979 1997 public service outline](https://saemobilus.sae.org/standards/j1979_199709-e-e-diagnostic-test-modes) · [Publicly accessible historical 2002 J1979 discovery reference](https://law.resource.org/pub/us/cfr/ibr/005/sae.j1979.2002.pdf). Use the current licensed standards for acceptance.
- **S12** · [SAE OBD standards catalog](https://saemobilus.sae.org/topics/electrical-electronics-and-avionics/on-board-diagnostics-obd) · [ISO 13400-2 DoIP](https://www.iso.org/standard/87961.html).
- **S13** · [OBDLink generic and enhanced diagnostics](https://support.obdlink.com/support/solutions/articles/43000713278-what-s-the-difference-between-obd-ii-standard-and-enhanced-diagnostics-).
- **S14** · [OBD Solutions STN2120 lifecycle status](https://www.obdsol.com/solutions/chips/stn2120/).
- **S15** · [Python-OBD command tables](https://python-obd.readthedocs.io/en/latest/Command%20Tables/) · [Independent example PID formulas](https://obd-data.com/pids/). These are cross-checks. Production definitions are from the applicable licensed annexes.
- **S16** · [SAE J1699/2 scan-tool cases](https://saemobilus.sae.org/standards/j16992_202412-test-cases-obd-ii-scan-tools-m-test-equipment) · [SAE J1699/3 vehicle cases](https://saemobilus.sae.org/standards/j16993_202608-vehicle-obd-ii-compliance-test-cases) · [Alliance OBD certification software](https://portal.autosinnovate.org/obd-certification-public-access/).
