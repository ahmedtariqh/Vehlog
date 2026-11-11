#!/usr/bin/env python3
"""Convert a text CAN capture to newline-delimited JSON or CSV."""

import argparse
import csv
import json
from pathlib import Path
import re
import sys


CANDUMP = re.compile(
    r"\((?P<time>\d+\.\d+)\)\s+(?P<channel>\S+)\s+"
    r"(?P<id>[0-9A-F]{3}|[0-9A-F]{8})(?P<sep>#{1,2})(?P<body>\S*)"
    r"(?:\s+(?P<direction>RX?|TX?))?",
    re.IGNORECASE,
)
ELM_FRAME = re.compile(
    r"(?P<id>[0-9A-F]{3}|[0-9A-F]{8}|(?:[0-9A-F]{2}\s+){3}[0-9A-F]{2})"
    r"(?P<body>\s+RTR|(?:\s+[0-9A-F]{2}){0,8})",
    re.IGNORECASE,
)
HEX = re.compile(r"[0-9A-F]*", re.IGNORECASE)
REMOTE = re.compile(r"R(?:(?P<raw>8_[9A-F])|(?P<dlc>[0-8]))?", re.IGNORECASE)
ELM_STATUS = {
    "BUFFER FULL": "capture_overrun",
    "CAN ERROR": "adapter_error",
    "BUS ERROR": "adapter_error",
    "DATA ERROR": "adapter_error",
    "<DATA ERROR": "adapter_error",
    "RX ERROR": "adapter_error",
    "LV RESET": "adapter_error",
    "NO DATA": "adapter_status",
    "STOPPED": "adapter_status",
    "OK": "adapter_status",
    "?": "adapter_status",
}
COLUMNS = (
    "line", "source", "type", "timestamp_s", "channel", "format", "can_id",
    "extended", "data_hex", "fd_flags", "dlc", "direction", "detail",
)


def parse_candump(text):
    match = CANDUMP.fullmatch(text)
    if not match:
        if text.startswith("DROPCOUNT:"):
            return {"type": "capture_overrun", "detail": text}
        raise ValueError("unrecognized compact candump record")

    fields = match.groupdict()
    can_id = fields["id"].upper()
    identifier = int(can_id, 16)
    is_error = len(can_id) == 8 and 0x20000000 <= identifier <= 0x3FFFFFFF
    extended = len(can_id) == 8 and not is_error
    if not is_error and identifier > (0x1FFFFFFF if extended else 0x7FF):
        raise ValueError("CAN identifier out of range")

    frame_type = "error_frame" if is_error else "frame"
    is_fd = fields["sep"] == "##"
    body = fields["body"].upper()
    flags = None
    dlc = None
    if is_fd:
        if is_error or not body or not HEX.fullmatch(body[0]):
            raise ValueError("invalid CAN FD header")
        flags, body = int(body[0], 16), body[1:]
    else:
        remote = REMOTE.fullmatch(body)
        if remote and not is_error:
            frame_type = "remote_frame"
            dlc = int(remote["raw"][-1], 16) if remote["raw"] else int(remote["dlc"] or 0)
            body = ""
        elif re.fullmatch(r"[0-9A-F]{16}_[9A-F]", body):
            body, raw_dlc = body.split("_")
            dlc = int(raw_dlc, 16)

    if not HEX.fullmatch(body) or len(body) % 2 or len(body) > (128 if is_fd else 16):
        raise ValueError("invalid CAN payload")

    direction = fields["direction"]
    return {
        "type": frame_type, "timestamp_s": fields["time"],
        "channel": fields["channel"], "format": "can_fd" if is_fd else "classic",
        "can_id": can_id, "extended": None if is_error else extended,
        "data_hex": body, "fd_flags": flags, "dlc": dlc,
        "direction": direction[0].upper() + "X" if direction else None,
    }


def parse_elm(text):
    text = text.lstrip(">").strip().upper()
    if not text:
        return None
    if text in ELM_STATUS:
        return {"type": ELM_STATUS[text], "detail": text}
    if text.startswith(("AT ", "ELM327", "STN")) or re.fullmatch(r"AT[A-Z0-9]+", text):
        return {"type": "adapter_status", "detail": text}

    match = ELM_FRAME.fullmatch(text)
    if not match:
        raise ValueError("expected spaced CAN monitor output with headers and no DLC")
    can_id = "".join(match["id"].split())
    extended = len(can_id) == 8
    if int(can_id, 16) > (0x1FFFFFFF if extended else 0x7FF):
        raise ValueError("CAN identifier out of range")
    body = "".join(match["body"].split())
    remote = body == "RTR"
    return {
        "type": "remote_frame" if remote else "frame",
        "channel": "elm0", "format": "classic",
        "can_id": can_id, "extended": extended, "data_hex": "" if remote else body,
    }


def read_records(lines, source):
    parser = parse_candump if source == "candump" else parse_elm
    for line_number, raw in enumerate(lines, 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        try:
            record = parser(line)
        except ValueError as error:
            record = {"type": "unparsed", "detail": f"{error}: {line}"}
        if record is not None:
            yield {"line": line_number, "source": source, **record}


def write_records(records, output, output_format):
    writer = csv.DictWriter(output, fieldnames=COLUMNS) if output_format == "csv" else None
    if writer:
        writer.writeheader()
    overruns = unparsed = frames = 0
    for record in records:
        overruns += record["type"] == "capture_overrun"
        unparsed += record["type"] == "unparsed"
        frames += record["type"] in {"frame", "error_frame", "remote_frame"}
        if writer:
            writer.writerow(record)
        else:
            print(json.dumps(record, separators=(",", ":")), file=output)
    print(f"{frames} frame(s), {overruns} overrun event(s), {unparsed} unparsed record(s)",
          file=sys.stderr)
    return 2 if overruns or unparsed else 0


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("input", help="Path to the capture, or - for stdin")
    cli.add_argument("--source", choices=("candump", "elm"), required=True)
    cli.add_argument("--format", choices=("ndjson", "csv"), default="ndjson")
    args = cli.parse_args()
    try:
        if args.input == "-":
            return write_records(read_records(sys.stdin, args.source), sys.stdout, args.format)
        with Path(args.input).open(encoding="utf-8-sig", errors="replace") as capture:
            return write_records(read_records(capture, args.source), sys.stdout, args.format)
    except OSError as error:
        print(f"vehlog: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
