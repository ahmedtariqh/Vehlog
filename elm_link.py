"""Read-only acquisition through a physical ELM327 serial interpreter."""

from base64 import b64encode
import json
import re
from time import monotonic, sleep

PROTOCOLS = tuple("0123456789ABC")
CAN_PROTOCOLS = frozenset("6789ABC")
HEX = re.compile(r"[0-9A-F]+\Z")
READ_LENGTHS = {0x01: 2, 0x02: 3, 0x03: 1, 0x05: 3,
                0x06: 2, 0x07: 1, 0x09: 2, 0x0A: 1}
MAX_REPLY = 128 * 1024
FAULT_TEXT = (b"NO DATA", b"UNABLE TO CONNECT", b"BUFFER FULL", b"BUS ERROR",
              b"CAN ERROR", b"DATA ERROR", b"FB ERROR", b"STOPPED", b"LV RESET")


def hex_text(value, widths):
    if value is None:
        return None
    value = value.upper()
    if len(value) not in widths or not HEX.fullmatch(value):
        raise ValueError(f"invalid hexadecimal argument {value!r}")
    return value


def obd_read(value, bench):
    compact = re.sub(r"[ \t]", "", value).upper()
    compact = hex_text(compact, (2, 4, 6))
    request = bytes.fromhex(compact)
    service = request[0]
    if service == 0x22 and len(request) == 3 and bench:
        return compact
    if service == 0x08 and len(request) == 2 and bench and request[1] % 32 == 0:
        return compact
    if READ_LENGTHS.get(service) != len(request):
        raise ValueError(f"unsupported read request {compact}")
    return compact


def evidence(data):
    return {
        "raw_b64": b64encode(data).decode("ascii"),
        "text": data.decode("ascii", errors="replace"),
    }


class ElmLink:
    def __init__(self, serial, *, clock=monotonic):
        self.serial = serial
        self.clock = clock

    def _write(self, data):
        if self.serial.write(data) != len(data):
            raise OSError("serial write was incomplete")

    def _reply(self, timeout):
        data = bytearray()
        until = self.clock() + timeout
        while self.clock() < until and len(data) < MAX_REPLY:
            data.extend(self.serial.read(128))
            if b">" in data:
                before, _, trailing = bytes(data).partition(b">")
                if trailing.strip():
                    raise OSError("unexpected data after adapter prompt")
                return before
        if len(data) >= MAX_REPLY:
            raise OSError("adapter reply exceeded the capture bound")
        raise OSError("adapter prompt timed out")

    def command(self, cmd, timeout=10):
        self._write(cmd.encode("ascii") + b"\r")
        return self._reply(timeout)

    def set(self, cmd):
        reply = self.command(cmd)
        if b"OK" not in {line.strip().upper() for line in reply.splitlines()}:
            raise OSError(f"adapter did not accept {cmd}: {reply[:300]!r}")
        return reply

    def monitor(self, duration, emit):
        self._write(b"ATMA\r")
        until = self.clock() + duration
        finished = False
        lost = False
        tail = b""
        try:
            while self.clock() < until:
                chunk = self.serial.read(256)
                if chunk:
                    emit({"kind": "monitor_chunk", **evidence(chunk)})
                    observed = (tail + chunk).upper()
                    lost |= any(marker in observed for marker in FAULT_TEXT)
                    tail = observed[-32:]
                    if b">" in chunk:
                        finished = True
                        break
        finally:
            if not finished:
                self._write(b"\r")
                stopped = self._reply(3)
                emit({"kind": "monitor_end", **evidence(stopped)})
                # STOPPED is the expected acknowledgement for our own interrupt.
                lost |= any(marker in stopped.upper() for marker in FAULT_TEXT if marker != b"STOPPED")
        return not (finished or lost)


def request_plan(args):
    if args.protocol not in PROTOCOLS:
        raise ValueError("invalid ELM protocol slot")
    if not 1200 <= args.baud <= 2_000_000:
        raise ValueError("invalid serial baud")
    if args.duration is not None and not 0.1 <= args.duration <= 3600:
        raise ValueError("monitor duration must be 0.1 to 3600 seconds")
    if args.duration is not None and (args.request or args.pgn or args.protocol == "0"):
        raise ValueError("monitoring requires an explicit protocol and excludes active requests")
    if args.request and args.pgn:
        raise ValueError("choose OBD reads or J1939 PGN requests")
    if len(args.request) + len(args.pgn) > 256:
        raise ValueError("too many requests for one acquisition")
    if args.pgn and args.protocol != "A":
        raise ValueError("PGN requests require protocol A")
    if args.request and args.protocol == "A":
        raise ValueError("protocol A uses PGN requests rather than OBD service requests")
    if args.request and args.protocol in {"B", "C"} and not args.bench:
        raise ValueError("user-defined CAN profiles are restricted to explicit bench use")
    custom = [args.tx_header, args.rx_filter, args.tx_address, args.rx_address]
    user1 = hex_text(args.user1, (4,))
    if user1 is not None and (not args.bench or args.protocol != "B" or args.duration is not None):
        raise ValueError("temporary USER1 configuration requires an active protocol B bench session")
    if any(custom) and (not args.bench or args.protocol not in CAN_PROTOCOLS):
        raise ValueError("custom CAN addressing requires an explicit CAN protocol and bench mode")
    if args.duration is not None and any(custom):
        raise ValueError("custom addressing applies to active bench reads only")

    requests = [obd_read(item, args.bench) for item in args.request]
    for item in args.pgn:
        pgn = hex_text(item, (6,))
        if int(pgn, 16) > 0x3FFFF:
            raise ValueError("PGN outside J1939 range")
        requests.append(pgn)
    if any(item.startswith("22") for item in requests) and args.request:
        if not args.tx_header or not args.rx_filter or args.protocol not in CAN_PROTOCOLS:
            raise ValueError("OEM DID reads require verified CAN transmit and response IDs")
    legacy = frozenset("12345")
    if any(item.startswith("05") for item in requests) and args.request and args.protocol not in legacy:
        raise ValueError("Mode 05 requires an explicitly selected legacy protocol")
    if args.rx_address and not args.tx_address:
        raise ValueError("an extended receive address requires an extended transmit address")

    settings = ["ATE0", "ATL0", "ATS1", "ATH1", f"ATTP{args.protocol}"]
    if user1 is not None:
        settings.insert(-1, "ATPB" + user1)
    if requests and args.protocol != "A":
        settings.append("ATAL")
    if args.duration is not None and args.protocol in CAN_PROTOCOLS:
        settings.extend(("ATCSM1", "ATCFC0"))
        if args.protocol == "A":
            settings.append("ATJHF0")
        else:
            settings.extend(("ATCAF0", "ATD1"))
    for value, command, widths in zip(custom, ("ATSH", "ATCRA", "ATCEA", "ATCER"),
                                      ((3, 8), (3, 8), (2,), (2,))):
        if value is not None:
            setting = hex_text(value, widths)
            if ((len(setting) == 8 and int(setting, 16) > 0x1FFFFFFF) or
                    (len(setting) == 3 and int(setting, 16) > 0x7FF)):
                raise ValueError("CAN identifier out of range")
            settings.append(command + setting)
    return settings, requests


def acquire(link, args, emit, *, pace=sleep):
    settings, requests = request_plan(args)
    banner = link.command("ATZ")
    emit({"kind": "adapter", "command": "ATZ", **evidence(banner)})
    if not any(label in banner.upper() for label in (b"ELM327", b"STN")):
        raise OSError("adapter reset banner was not recognized")
    for setting in settings:
        emit({"kind": "setting", "command": setting, **evidence(link.set(setting))})
    complete = True
    for index, cmd in enumerate(requests):
        if index:
            pace(0.12)
        reply = link.command(cmd, timeout=35)
        upper = reply.upper()
        incomplete = (not upper.strip() or upper.strip() == b"?" or
                      any(marker in upper for marker in FAULT_TEXT))
        complete &= not incomplete
        emit({"kind": "response", "command": cmd,
              "status": "incomplete" if incomplete else "reply", **evidence(reply)})
    if args.duration is not None:
        complete = link.monitor(args.duration, emit)
    for cmd in ("ATDPN", "ATRV", "ATIGN", "ATAMC", "ATCS"):
        emit({"kind": "adapter", "command": cmd, **evidence(link.command(cmd))})
    return complete


def add_arguments(parser):
    parser.add_argument("port", help="USB/serial port or Bluetooth serial port exposed by the OS")
    parser.add_argument("--baud", type=int, default=38400)
    parser.add_argument("--protocol", choices=PROTOCOLS, default="0")
    parser.add_argument("--request", action="append", default=[], metavar="HEX")
    parser.add_argument("--pgn", action="append", default=[], metavar="HEX")
    parser.add_argument("--duration", type=float, help="seconds of passive monitoring")
    parser.add_argument("--bench", action="store_true", help="allow fixture-only read configurations")
    parser.add_argument("--user1", help="temporary USER1 options and bitrate divisor in hex")
    parser.add_argument("--tx-header")
    parser.add_argument("--rx-filter")
    parser.add_argument("--tx-address")
    parser.add_argument("--rx-address")


def run_elm(args):
    request_plan(args)
    try:
        import serial
    except ImportError as error:
        raise ValueError("physical adapter acquisition requires pyserial") from error
    with serial.Serial(args.port, args.baud, timeout=0.05, write_timeout=2) as device:
        device.reset_input_buffer()
        complete = acquire(ElmLink(device), args,
                           lambda record: print(json.dumps(record, separators=(",", ":")), flush=True))
    return 0 if complete else 2
