"""Decode captured Classical J1939 and assemble recorded BAM and RTS transfers."""

from dataclasses import dataclass, field
from decimal import Decimal
import json

from capture_reader import read_records

PGN_CM = 0xEC00
PGN_DT = 0xEB00
PGN_DM1 = 0xFECA
MAX_SESSIONS = 64
# Offline evidence cutoff, not a claim about normative sender/receiver timers.
SESSION_GAP = Decimal("2.0")


@dataclass
class Transfer:
    pgn: int
    size: int
    packets: int
    method: str
    priority: int
    start_line: int
    last_time: Decimal
    payload: bytearray = field(default_factory=bytearray)
    next_packet: int = 1


def dm1_fields(payload):
    if len(payload) < 2:
        return {}
    dtcs = []
    for start in range(2, len(payload) - 3, 4):
        entry = payload[start:start + 4]
        if entry not in (b"\x00" * 4, b"\xFF" * 4):
            first, second, third, fourth = entry
            legacy = bool(fourth & 0x80)
            dtcs.append({
                "spn": None if legacy else first | (second << 8) | ((third & 0xE0) << 11),
                "fmi": third & 0x1F,
                "occurrence_count": fourth & 0x7F,
                "legacy_conversion": legacy,
                "raw": entry.hex().upper(),
            })
    return {"lamps": payload[0], "lamp_flash": payload[1], "dtcs": dtcs}


def message(key, pgn, payload, line, priority, transport="single"):
    channel, source, destination = key
    result = {
        "kind": "message", "channel": channel, "source": source, "destination": destination,
        "pgn": pgn, "data_hex": payload.hex().upper(), "line": line,
        "priority": priority, "transport": transport,
    }
    if pgn == PGN_DM1:
        result.update(dm1_fields(payload))
    return result


def incomplete(key, transfer, reason):
    result = message(key, transfer.pgn, transfer.payload, transfer.start_line,
                     transfer.priority, transfer.method)
    for key_name in ("lamps", "lamp_flash", "dtcs"):
        result.pop(key_name, None)
    return {**result, "kind": "incomplete", "reason": reason,
            "expected_size": transfer.size}


def captured_messages(records):
    sessions = {}
    last_times = {}
    for record in records:
        kind = record["type"]
        if kind in {"capture_overrun", "error_frame", "unparsed"}:
            yield {"kind": "capture_gap", "line": record["line"], "reason": kind}
            for key in list(sessions):
                if not record.get("channel") or key[0] == record["channel"]:
                    yield incomplete(key, sessions.pop(key), kind)
            continue
        if (kind != "frame" or not record["extended"] or record["format"] != "classic" or
                record.get("direction") == "TX"):
            continue

        line = record["line"]
        identifier = int(record["can_id"], 16)
        channel = record["channel"]
        now = Decimal(record["timestamp_s"])
        if channel in last_times and now < last_times[channel]:
            for key in list(sessions):
                if key[0] == channel:
                    yield incomplete(key, sessions.pop(key), "timestamp reset")
            yield {"kind": "capture_gap", "line": line, "reason": "timestamp reset"}
        last_times[channel] = now
        for key in list(sessions):
            if key[0] == channel and now - sessions[key].last_time > SESSION_GAP:
                yield incomplete(key, sessions.pop(key), "inter-packet gap")

        source = identifier & 0xFF
        pdu_format = (identifier >> 16) & 0xFF
        destination = (identifier >> 8) & 0xFF if pdu_format < 240 else 255
        pgn = (identifier >> 8) & 0x3FFFF
        if pdu_format < 240:
            pgn &= 0x3FF00
        key = (channel, source, destination)
        priority = (identifier >> 26) & 7
        data = bytes.fromhex(record["data_hex"])

        if pgn == PGN_CM:
            if len(data) != 8:
                yield {"kind": "capture_gap", "line": line, "reason": "malformed TP.CM"}
                for direction in (key, (channel, destination, source)):
                    if direction in sessions:
                        yield incomplete(direction, sessions.pop(direction), "malformed TP.CM")
                continue
            control = data[0]
            target_pgn = int.from_bytes(data[5:8], "little")
            yield {**message(key, pgn, data, line, priority), "kind": "transport_control"}
            if control in (0x10, 0x20):
                size = int.from_bytes(data[1:3], "little")
                packets = data[3]
                if key in sessions:
                    yield incomplete(key, sessions.pop(key), "superseded")
                if (not 9 <= size <= 1785 or packets != (size + 6) // 7 or
                        target_pgn > 0x3FFFF or
                        ((control == 0x20) != (destination == 255)) or
                        len(sessions) >= MAX_SESSIONS):
                    yield {"kind": "capture_gap", "line": line, "reason": "unsupported or malformed TP start"}
                    continue
                sessions[key] = Transfer(target_pgn, size, packets,
                                         "bam" if control == 0x20 else "rts", priority, line, now)
            elif control == 0xFF:
                for direction in (key, (channel, destination, source)):
                    active = sessions.get(direction)
                    if active is not None and active.pgn == target_pgn:
                        yield incomplete(direction, sessions.pop(direction), "transport abort")
        elif pgn == PGN_DT:
            active = sessions.get(key)
            if active is None:
                yield {"kind": "capture_gap", "line": line, "reason": "unmatched TP.DT",
                       **{k: v for k, v in message(key, pgn, data, line, priority).items()
                          if k != "kind"}}
            elif len(data) != 8 or data[0] != active.next_packet:
                yield incomplete(key, sessions.pop(key), "unexpected TP.DT sequence or length")
            else:
                remaining = active.size - len(active.payload)
                active.payload.extend(data[1:1 + min(7, remaining)])
                active.last_time = now
                active.next_packet += 1
                if data[0] == active.packets:
                    sessions.pop(key)
                    yield message(key, active.pgn, active.payload, active.start_line,
                                  active.priority, active.method)
        else:
            yield message(key, pgn, data, line, priority)

    for key, active in sessions.items():
        yield incomplete(key, active, "capture ended")


def run_j1939(path):
    complete = True
    with path.open(encoding="utf-8-sig", errors="replace") as stream:
        for record in captured_messages(read_records(stream, "candump")):
            print(json.dumps(record, separators=(",", ":")))
            if record["kind"] in {"capture_gap", "incomplete"}:
                complete = False
    return 0 if complete else 2
