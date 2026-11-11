"""Validate and group diagnostic console records from one Vehlog bench scan."""

from hashlib import sha256
import json
from pathlib import Path
import re

UNITS = frozenset({"", "rpm", "km/h", "degC", "%"})
STATES = frozenset({"supported_page", "data", "negative", "no_response", "incomplete"})
MAX_PDU_BYTES = 4095
MAX_LINE = (MAX_PDU_BYTES * 2) + 220
HEX = re.compile(rf"(?:[0-9A-Fa-f]{{2}}){{0,{MAX_PDU_BYTES}}}\Z")
SERVICES = {1, 2, 3, 6, 7, 9, 10}
DTC_CATEGORIES = {3: "stored", 7: "pending", 10: "permanent"}


def integer(row, key, low, high):
    value = row.get(key)
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f"invalid {key}")
    return value


def parse_diagnostic(row, catalog=None):
    service = integer(row, "service", 1, 10)
    if service not in SERVICES:
        raise ValueError("unsupported diagnostic service")
    extended = row.get("extended")
    if type(extended) is not bool:
        raise ValueError("invalid extended flag")
    ecu = integer(row, "ecu", 0, 0x1FFFFFFF if extended else 0x7FF)
    pid = integer(row, "pid", 0, 255)
    bitmap = integer(row, "bitmap", 0, 0xFFFFFFFF)
    nrc = integer(row, "nrc", 0, 255)
    clock = integer(row, "mono_us", 0, 2**64 - 1)
    value = integer(row, "value_milli", -(2**31), 2**31 - 1)
    status, unit, pdu = (row.get(key) for key in ("status", "unit", "pdu"))
    if (not all(isinstance(item, str) for item in (status, unit, pdu)) or
            status not in STATES or unit not in UNITS or not HEX.fullmatch(pdu)):
        raise ValueError("invalid status, unit or payload")
    if unit and status != "data":
        raise ValueError("scaled values require a data response")
    observation = {
        "pid": pid, "status": status, "mono_us": clock or None,
        "bitmap": bitmap if status == "supported_page" else None,
        "nrc": nrc if status == "negative" else None,
        "metric": {"unit": unit, "value_milli": value} if unit else None,
        "pdu": pdu.upper() if pdu else None,
    }

    if service != 1:
        observation["service"] = service
        if service == 2:
            observation["freeze_frame"] = integer(row, "freeze_frame", 0, 255)
        if status == "data":
            observation.update(decode_identified_reply(service, pid, bytes.fromhex(pdu)))
    if catalog is not None and status == "data":
        payload = bytes.fromhex(pdu)
        has_frame = service != 2 or (len(payload) >= 3 and payload[2] == observation["freeze_frame"])
        decoded = catalog.decode(service, pid, payload) if has_frame else []
        if decoded:
            observation["catalog_revision"] = catalog.revision
            observation["catalog_metrics"] = decoded
    return observation


def decode_identified_reply(service, pid, pdu):
    if not pdu or pdu[0] != service + 0x40:
        return {}
    if service in DTC_CATEGORIES and len(pdu) >= 2 and len(pdu) == 2 + (2 * pdu[1]):
        codes = []
        for first, second in zip(pdu[2::2], pdu[3::2]):
            if first or second:
                codes.append(f'{"PCBU"[first >> 6]}{first & 0x3F:02X}{second:02X}')
        return {"dtc_category": DTC_CATEGORIES[service], "dtcs": codes}
    if service == 9 and pid == 2 and len(pdu) == 20 and pdu[1:3] == b"\x02\x01":
        vin = pdu[3:].decode("ascii", errors="replace")
        if re.fullmatch(r"[A-HJ-NPR-Z0-9]{17}", vin):
            return {"vin": vin}
    return {}


def inspect_diagnostics(path, catalog=None):
    digest = sha256()
    ecus = {}
    scan_end = None
    with Path(path).open("rb") as stream:
        for line_no in range(1, 8194):
            line = stream.readline(MAX_LINE + 1)
            if not line:
                break
            if line_no > 8192 or len(line) > MAX_LINE:
                raise ValueError("diagnostic log exceeds the line or row limit")
            digest.update(line)
            if not line.strip():
                continue
            try:
                row = json.loads(line)
                if not isinstance(row, dict):
                    raise ValueError("expected a version 1 diagnostic record")
                integer(row, "v", 1, 1)
                if scan_end is not None:
                    raise ValueError("a scan_end record must end this single-session log")
                if row.get("record") == "scan_end":
                    stopped = row.get("stopped")
                    if type(stopped) is not bool:
                        raise ValueError("invalid stopped flag")
                    scan_end = {"status_code": integer(row, "status_code", 0, 5), "stopped": stopped}
                elif row.get("record") == "diag":
                    observation = parse_diagnostic(row, catalog)
                    ecus.setdefault((row["extended"], row["ecu"]), []).append(observation)
                else:
                    raise ValueError("unknown diagnostic record")
            except (ValueError, RecursionError) as error:
                raise ValueError(f"diagnostic log line {line_no}: {error}") from error
    return {
        "sha256": digest.hexdigest(),
        "scan_end": scan_end,
        "ecus": [
            {"extended": extended, "ecu": ecu, "observations": records}
            for (extended, ecu), records in sorted(ecus.items())
        ],
    }
