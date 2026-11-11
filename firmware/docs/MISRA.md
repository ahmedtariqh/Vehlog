# MISRA C:2025 compliance plan

## Claim scope and current status

The target is MISRA C:2025 for project-owned production C. The protocol core is compiled as ISO C11 with bounded storage, no dynamic allocation, explicit widths, fixed-point values and controlled dependency direction. The ESP-IDF adapter and composition compile as GNU C11 to accommodate SDK headers. The compliance review must account for every extension actually used. Strict GCC diagnostics, GCC's static analyzer and host UBSan fixtures provide implementation evidence. They do not establish MISRA compliance. Application firmware, both Kconfig profiles, SDK and toolchain integration, and third-party components need a completed project compliance record before a claim can be made.

## Release evidence

| Evidence | Contents and acceptance |
|---|---|
| Guidelines Re-categorization Plan | Specify the applicable licensed edition, project scope and agreed guideline categories. Identify source, generated code, build variants and adopted code. |
| Guidelines Enforcement Plan | Record every applicable directive and rule, enforcement method, tool/version/configuration, review evidence, and any gap that needs manual examination. Qualify coverage of both ESP32 build profiles using their actual compile databases. |
| Deviations | Record rule or directive, exact lines/configuration, rationale, scope, risks, compensating checks, approver and review date. Treat deviations as open until reviewed. |
| Adopted code assessment | Inventory ESP-IDF, FreeRTOS, drivers, compiler runtime, libc, modem libraries and generated configuration. Pin versions. Document interfaces, upstream evidence, errata, assumptions, and integration validation. |
| Verification | Run host fixtures, fuzz malformed transport data, build both configurations, inspect stack and timing evidence, run the HIL matrix, and retain results tied to source and binaries. |
| Guidelines Compliance Summary | Report per-guideline findings and deviation IDs for the exact baseline. Block a compliance claim until the applicable obligations and reviews are complete. |

The C11 text encoder is an output boundary and requires assessment of its standard-library formatting and string calls. Open assessment items include ESP-IDF bit fields and GPIO/RTOS macros at the adapter boundary, task notification and ISR interactions, the vendor driver's allocation, and formatted I/O in the bench application. Review modular arithmetic in the 32-bit deadline clock, array and index invariants in transport parsing, and the ISR stack footprint. Keep SDK and RTOS headers out of `vl_core` and constrain any deviations to their reviewed scope.

References. [MISRA C editions](https://misra.org.uk/misra-c/) · [MISRA Compliance](https://misra.org.uk/compliance/) · [MISRA Compliance:2020](https://www.misra.org.uk/app/uploads/2021/06/MISRA-Compliance-2020.pdf)
