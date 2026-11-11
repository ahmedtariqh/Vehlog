# Vehlog build plan

Vehlog pairs peer-to-peer car rental with a tracker you own. The first product test is whether an owner can hand over a car, see the state of the rental, receive actionable warnings, and settle a return with the renter using the same evidence. Assume an Egypt-first pilot, personally installed trackers, and in-person key handover. Treat the pilot as invitation-only while the booking, insurance, tracking and claims workflows are validated.

## What the app can show

| Signal | First usable source | How it appears |
|---|---|---|
| Location, route, speed and trip movement | Tracker GNSS | Last fix, fix quality and age. A stale value retains its timestamp and is labelled. |
| Rental usage and estimated distance | Valid GNSS segments inside the rental window | Distance estimate with explicit gaps. A checkout/check-in odometer photo is separate evidence. |
| Scheduled return, time extension and check-in | Booking service | Lease window and state, including an overdue alert independent of device connectivity. |
| Power loss, tracker offline and vehicle supply voltage | Tracker hardware, power sense and server heartbeats | Distinct events. Backup power or loss of contact does not establish removal. |
| Harsh braking, motion while parked and impact candidates | Calibrated IMU and vehicle mount, with GNSS when available | Reviewable event with signal quality. Validate false alarms for potholes and mounting changes. |
| Engine RPM, coolant temperature, emissions DTCs, fuel level | Applicable OEM data or separately approved diagnostics | Only on a vehicle/profile where the specific signal and access path were verified. Availability varies even across vehicles with the same connector. [SAE J1979](https://saemobilus.sae.org/standards/j1979_201202-e-e-diagnostic-test-modes) |
| Doors, TPMS, odometer, EV charge and charging state | Authorized OEM integration or validated vehicle signal profile | Show the source, profile and last observation. Test each powertrain and ECU generation. [SAE ZEVonUDS](https://saemobilus.sae.org/standards/j1979-3_202212-e-e-diagnostic-test-modes-zero-emission-vehicle-propulsion-systems-uds-zevonuds) |

Every signal has a source, timestamp and quality. Separate `unsupported`, `supported but unavailable`, `live` and `stale`. Keep GNSS location and rental rules available for cars with no useful diagnostic interface. An OBD connector may only expose gateway-approved traffic or request/reply diagnostics. Internal ECU logs require a documented diagnostic service or OEM data path and some never leave the ECU. CAN frames only describe traffic observed on the tapped network. [SAE J1979](https://saemobilus.sae.org/standards/j1979_201202-e-e-diagnostic-test-modes) · [Stellantis diagnostic access](https://www.technicalinformation.fiat.com/tech-info-web/web/pageLarge.do?id=356) · [Linux SocketCAN](https://docs.kernel.org/networking/can.html)

## Reverse engineering evidence ledger

The GitHub investigation workflow separates observed artifacts, inferences and unknowns. The attached replay is an original implementation. Public specifications establish reference behavior. The exact adapter's components, firmware, bus visibility and loss rate need physical evidence. [Reverse Engineer Anything](https://github.com/Speedrunlab/reverse-engineer-anything/blob/main/SKILL.md) · [Firmware workflow](https://github.com/Speedrunlab/reverse-engineer-anything/blob/main/references/firmware-and-archives.md)

| ID | Status | Finding or question | Evidence or next capture |
|---|---|---|---|
| RE-01 | Documented reference | The genuine ELM327 translates between an AT-style interface and supported diagnostic transports. Its serial monitor can overflow, depending on traffic and settings. | [ELM327 datasheet, monitor and maximum data rates](https://elmelectronics.com/wp-content/uploads/2020/05/ELM327DSL.pdf) |
| RE-02 | Open | Identify the exact ELM adapter, controller, physical-layer chips, connector wiring and programming pads. | Both PCB faces, chip markings, adapter label, transport type and measured bench pin map. A reported `ATI` string is supporting evidence only. |
| RE-03 | Open | Determine behavior and frame losses of the actual adapter under known loads. | Compare the adapter transcript against a simultaneous independent capture. Record bitrates, IDs, timing, payload patterns, load and overruns. |
| RE-04 | Open | Determine which signals are accessible and independently reproducible for the first car. | Capture a controlled session on an authorized vehicle, pair changes with independent reference measurements, and hold out another trip for validation. |
| RE-05 | Design choice | Vehlog owns its device protocol, field firmware and decoder versions. An optional lab adapter can preserve familiar capture formats. | This bundle defines a versioned pilot input and a raw ELM/SocketCAN reader. |
| RE-06 | Verified in software | The pilot recovers an ordered rental replay from retransmitted samples and detects six distinct demo events. The CAN/ELM reader preserves source information and surfaces a known ELM overrun. | Included fixtures and automated tests. |

For each authorized physical sample, record board revision, source, date, setup, firmware identifiers and SHA-256 of each original capture. Begin with board identification and passive bench observations. If a readable, authorized firmware image is available, hash it and examine a copy in a bounded offline environment. Use the identified controller's documented interface and keep the original hardware available for A/B comparison. If readback is locked, characterize the adapter from its documented external interfaces and use a documented MCU for the custom product. See [bench notes](ELM327_Bench_Notes.md) for the reference AT transcript setup.

A vehicle decoder profile needs the make, model, model year range, region, powertrain, ECU fingerprint, bus, arbitration ID and verified bit mapping. Record length, signedness, endianness, scale, units, ranges and sampling cadence, including multiplexer and validity fields when applicable. Compare against independent sources, such as a dashboard indication or approved diagnostic tool, then test on a separate recording and another equivalent car. Version the decoder separately from firmware. Reprocess raw captures when a mapping changes. Check reuse terms before shipping any external DBC or OEM content. [opendbc reference repository](https://github.com/commaai/opendbc)

## Hardware and firmware

**Mandatory release requirement.** [VEHLOG-ACQ-001](firmware/docs/ELM327_PARITY.md) requires genuine ELM327 v2.3 data-acquisition parity. Include K/L-Line, J1850 PWM and VPW, J1939, configurable CAN, the applicable OBD-II read services, and adapter-side measurements in the design and acceptance plan. Close the [bench acceptance matrix](firmware/docs/ELM327_PARITY_MATRIX.csv) before claiming parity.

The embedded baseline now targets [ESP32-C5, ESP-IDF and FreeRTOS](firmware/README.md). See the firmware coverage matrix for implemented diagnostic modes and release gates.

Start two builds in parallel only once both have a bench owner. The field tracker can reach a rental pilot using GNSS first. The lab logger earns vehicle coverage without delaying that pilot.

| Module | First revision | Verification |
|---|---|---|
| Field tracker core | Protected 12 V prototype, documented MCU, GNSS, LTE Cat 1 bis candidate modem, 6-axis IMU, nonvolatile event queue and provisioned device identity. Wired USB for lab service. | Bench modem and GNSS performance on the intended SIM and carrier, ignition-on/off behavior, peak transmit current, thermal and storage endurance. Choose the modem SKU with the carrier and local approval path. |
| Vehicle interface | ESP32-C5 has two TWAI controllers for Classical CAN and CAN FD. The baseline uses one, with the second reserved for a separately validated channel. Use an external TCAN1051GV-Q1 or a reviewed equivalent per channel. Verify package pin assignments, clock, logic levels and supported bit rates. | Controller in bus monitor mode plus hardware transmit disabled across boot, reset, fault, OTA and brownout. Review the reset pin states, board loading and unpowered bus leakage. [ESP32-C5 TWAI](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32c5/api-reference/peripherals/twai.html) · [TI TCAN1051-Q1](https://www.ti.com/product/TCAN1051-Q1) |
| Power and enclosure | Fused input, rated protection for reverse battery, vehicle transients and load dump, brownout supervision, and a separately qualified hold-up or backup design. Field device installs with an approved protected harness. | Specify the electrical test limits before routing, bench-test sleep and transmit modes, then validate current draw at the installed harness. Keep a 24 V truck variant a separate validation target. [TI automotive input protection](https://www.ti.com/tool/TIDA-01167) |
| Raw capture | Timestamp frames on a common monotonic clock, record channel, direction when known, ID and width, flags, length, bytes, clock synchronization markers and hardware/software drop counters. | Compare two channels against a reference at agreed load and bit rates. Force buffer overflow, storage pressure, reboot and a sudden power cut. Confirm loss is counted or the session is marked incomplete. |
| Device firmware | Separate sampler, power manager, durable queue, modem task and updater. Signed releases with recovery image, key rotation and a tested update rollback path. | Verify uptime, event ordering, acknowledgements, backoff and recovery with fault injection. Test configurations and staged releases against isolated bench fixtures. |
| Vehicle communication boundary | Field rental hardware stays sensor-only or physically receive-only. Keep diagnostic transmission on a separate controlled lab harness or reviewed service tool. No cellular path to arbitrary CAN frames. | Validate electrical silence with an independent analyzer and characterize startup, standby, shutdown and fault behavior before use on a car. [NHTSA automotive cybersecurity guidance](https://www.nhtsa.gov/sites/nhtsa.gov/files/2022-09/cybersecurity-best-practices-safety-modern-vehicles-2022-tag.pdf) |

Lab architecture can support additional electrical front ends and harnesses over time. Start with accessible CAN and CAN FD. K-Line and both J1850 variants are required for the ELM327 parity release. Qualify their physical layers separately. Heavy-duty J1939 needs a verified harness and power variant. DoIP needs compatible Ethernet hardware and authorized diagnostic access. Track support by vehicle and signal, and distinguish passive broadcast from active diagnostics. [Linux J1939](https://docs.kernel.org/networking/j1939.html) · [ISO 13400-2 DoIP](https://www.iso.org/standard/87961.html)

## Telemetry and alerts

Device firmware emits versioned observations to a private TLS endpoint, with a unique device certificate, boot ID, sequence, monotonic tick, UTC estimate and time quality. The server binds device identity to the enrolled vehicle and persists events before acknowledging a batch. A bounded on-device queue retains readings during coverage loss. When capacity is exhausted, preserve priority events where possible and report the lost range. For Wi-Fi or carrier outages, the device can still log locally. The app shows the last known state, and the backend labels late arrivals separately. A power cut can only be reported immediately if backup energy and connectivity remain.

Start with a modular backend, PostgreSQL for rental and structured events, a durable notification outbox and private storage for authorized engineering captures. Keep separate vehicle assignments and rental intervals. Resolve assignment and booking at event time and reject telemetry with ambiguous clock quality from compliance rules. Use a small holdback for event-time processing, deduplicate by device/boot/sequence, then record any post-holdback events as backfilled. A late upload can update history without emitting a push labelled live. Keep booking updates and their outbox records in one transaction.

Store the rules in a versioned rental policy snapshot. Both owner and renter see the agreed limits before checkout. If the rental is extended or a term is updated, record the effective time, who approved it, and the new policy revision. Proposed defaults are for pilot calibration.

| Rule | Suggested pilot behavior |
|---|---|
| Speed | Evaluate the agreed rental speed cap using high-quality samples. Require a configured tolerance and dwell period. Clear on confirmed recovery and rate-limit repeats. A legal road speed rule would need verified map matching, current limits and jurisdiction-specific design. |
| Geofence | Require consecutive fixes confidently outside the boundary. Use recovery hysteresis and suppress state changes when GNSS is uncertain. Apply the rule to an agreed service area. |
| Mileage | Notify at an agreed share of the allowance and again at the limit. Report distance quality and gaps. Resolve disputed mileage with the handover evidence. |
| Return | Calculate approaching due and overdue from the server clock and active booking. Booking extension and check-in cancel superseded scheduled alerts. |
| Offline and power | Use time since last expected heartbeat and the known ignition/sleep mode. Distinguish offline, connection restored, vehicle power lost and confirmed tamper sensor events. |
| Fault and incident | Use vehicle-specific DTC or verified sensor source. Keep an incident window for support review, with time, source, rule version, device confidence and delivery history. |

Notify the renter and owner where appropriate. In the first pilot, alerts inform decisions and create a review item. Put damage, fees and disputes through a human review flow with access to relevant evidence. Keep any action on the vehicle outside the rental firmware.

## Rental app and operational controls

1. **Owner** verifies their identity and right to list the car, pairs a tracker, sees capabilities, sets rental terms, publishes availability and reviews booking requests.
2. **Renter** verifies eligibility, selects a time, reviews the price and tracking disclosure, acknowledges the rules, pays through an approved provider, and receives pickup instructions.
3. **Checkout** records timestamps, photos, odometer and fuel or charge. Both parties confirm handover. The booking moves from confirmed to active, and the platform activates its rental-scoped telemetry view.
4. **Trip** shows position and freshness, usage versus limits, warnings and event history. Renter sees their terms and applicable events, and can contact support or request an extension.
5. **Return** records condition, odometer and fuel or charge. The booking closes the live rental view and settlement begins. Support can review authorized evidence with access logging. Provider webhooks and booking transitions must be idempotent.

Use Flutter as the proposed owner/renter app framework and a private admin console. Vehicle ownership and active rental are distinct permissions. Use server-side tenant isolation, short-lived sessions, audit logs for precise location access, encryption and a retention schedule per data category. Pre-booking disclosure should show data collected, who sees it, purposes, duration and how to challenge an alert. Keep precise routes scoped to agreed use and give support time-bound access. Establish a rental agreement, claims process, payment and payout rules, and written insurance coverage for the intended rental model before opening bookings. A commercial precedent separates monitoring-device disclosure and guest protections. [Turo tracking policy](https://help.turo.com/tracking-and-technology-devices-SkF48Nl49)

For an Egypt pilot, verify the current obligations with local counsel, the Personal Data Protection Center, and an approved connectivity or vehicle-tracking provider. NTRA publishes separate considerations for M2M device manufacture/import, type approval, and automatic vehicle location. Resolve the required operator arrangement, device approval, data handling and any licensing before deploying field trackers. Evaluate geolocation, identity data, transfers and retention under the applicable data protection rules. [NTRA IoT framework](https://www.tra.gov.eg/wp-content/uploads/2023/08/IoT-Framework-En.pdf) · [NTRA type approval FAQ](https://www.tra.gov.eg/wp-content/uploads/2024/03/FAQ.pdf) · [Egypt PDPC](https://pdpc.gov.eg/) · [Executive Regulations text](https://masaar.net/ar/egypt_laws/%D8%A7%D9%84%D9%84%D8%A7%D8%A6%D8%AD%D8%A9-%D8%A7%D9%84%D8%AA%D9%86%D9%81%D9%8A%D8%B0%D9%8A%D8%A9-%D9%84%D9%82%D8%A7%D9%86%D9%88%D9%86-%D8%AD%D9%85%D8%A7%D9%8A%D8%A9-%D8%A7%D9%84%D8%A8%D9%8A/)

## Milestones and acceptance targets

| Milestone | Exit criteria |
|---|---|
| 0. Offline replay | Reproduce a trip with deliberate gaps, duplicate upload, poor GNSS and late return. Reproduce overrun evidence from an ELM transcript. Included. |
| 1. Bench capture and device telemetry | Identify the exact ELM unit. Compare its capture to a known reference. Obtain boot, sequence, power state and GNSS from the tracker bench build. Force reboots, queue overflow and upload retries. Verify zero logger-generated dominant bits, including ACKs, on the specified bench matrix. |
| 2. One-car prototype | Enroll one authorized car, show fresh/stale position, test checkout/return and replay an outage. Define sleep-current and power recovery limits from the actual install. Candidate active interval is 5 seconds, with a target of 15 seconds at the 95th percentile from capture to owner display when data service and GNSS are available. Record distributions for the outage cases separately. |
| 3. Closed rental pilot | A small invited cohort completes booking through settlement and disputes. Test double-booking, cross-account access, extension races, alert duplicates, delayed uploads, stale GPS, false geofence crossings and power transitions. Track actionable alert rate, false alert rate, completed rentals, owner response, and support resolution. |
| 4. Vehicle-specific coverage | Publish a tested capability matrix and add the next models based on actual demand. Validate every advertised signal across revisions and regions. Pass the device approvals, privacy, insurance and security review for the launch market. |

Keep the PCB schematics and layout, test fixtures, bootloader and application firmware, device protocol, app and backend code, and provisioning/signing process in repositories you control. Separate lab builds from field builds and track firmware dependencies, modem licenses and any external decoder terms. The highest-leverage next inputs are photos and markings from both sides of the ELM327 board, its transport type, and the make, model, year, market and powertrain of the first test vehicle.
