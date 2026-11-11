"""Read the Vehlog pilot's device log and compact SocketCAN captures."""

from collections import Counter
from dataclasses import asdict, dataclass
from datetime import UTC, datetime
from hashlib import sha256
import json
from math import isfinite
from pathlib import Path
import re

from capture_reader import read_records

ID = re.compile(r"[A-Za-z0-9_-]{1,64}\Z")


def timestamp(value):
    if not isinstance(value, str):
        raise ValueError("expected an ISO 8601 timestamp with a timezone")
    result = datetime.fromisoformat(value)
    if result.utcoffset() is None:
        raise ValueError("timestamp must include a timezone")
    return result.astimezone(UTC)


def identifier(value, field):
    if not isinstance(value, str) or not ID.fullmatch(value):
        raise ValueError(f"invalid {field}")
    return value


def number(value, field, minimum, maximum):
    if type(value) not in (int, float) or not minimum <= value <= maximum or not isfinite(value):
        raise ValueError(f"{field} must be numeric and between {minimum} and {maximum}")
    return float(value)


@dataclass(frozen=True)
class Fix:
    lat: float
    lon: float
    accuracy_m: float


@dataclass(frozen=True)
class Sample:
    device: str
    boot: str
    seq: int
    time: datetime
    fix: Fix | None
    speed_kph: float | None
    power: str | None

    def json(self):
        result = asdict(self)
        result["time"] = self.time.isoformat()
        return result


def parse_sample(row):
    if not isinstance(row, dict) or type(row.get("v")) is not int or row["v"] != 1:
        raise ValueError("expected a version 1 device sample")
    device = identifier(row.get("device"), "device")
    boot = identifier(row.get("boot"), "boot")
    seq = row.get("seq")
    if type(seq) is not int or not 0 <= seq < 2**32:
        raise ValueError("seq must be an unsigned 32-bit integer")
    time = timestamp(row.get("time"))
    raw_fix = row.get("fix")
    fix = None
    if raw_fix is not None:
        if not isinstance(raw_fix, dict):
            raise ValueError("fix must be an object or null")
        fix = Fix(number(raw_fix.get("lat"), "lat", -90, 90),
                  number(raw_fix.get("lon"), "lon", -180, 180),
                  number(raw_fix.get("accuracy_m"), "accuracy_m", 0.1, 10000))
    speed = row.get("speed_kph")
    if speed is not None:
        if fix is None:
            raise ValueError("GNSS speed requires a GNSS fix")
        speed = number(speed, "speed_kph", 0, 500)
    power = row.get("power")
    if power not in (None, "vehicle", "backup"):
        raise ValueError("power must be vehicle, backup or null")
    return Sample(device, boot, seq, time, fix, speed, power)


def read_samples(path, device):
    by_identity = {}
    with Path(path).open("rb") as stream:
        for line_no in range(1, 100_002):
            line = stream.readline(8193)
            if not line:
                break
            if line_no == 100_001 or len(line) > 8192:
                raise ValueError("device log exceeds the pilot line or row limit")
            if not line.strip():
                continue
            try:
                sample = parse_sample(json.loads(line))
                if sample.device != device:
                    raise ValueError("sample belongs to a different device")
                key = (sample.boot, sample.seq)
                if key in by_identity and by_identity[key] != sample:
                    raise ValueError("conflicting samples share the same boot and sequence")
                by_identity[key] = sample
            except (ValueError, RecursionError) as error:
                raise ValueError(f"device log line {line_no}: {error}") from error
    ordered = sorted(by_identity.values(), key=lambda sample: (sample.time, sample.boot, sample.seq))
    latest_seq = {}
    for previous, current in zip(ordered, ordered[1:]):
        if previous.time == current.time:
            raise ValueError("distinct samples share a capture timestamp")
    for sample in ordered:
        if sample.seq <= latest_seq.get(sample.boot, -1):
            raise ValueError("capture time disagrees with sequence within a boot")
        latest_seq[sample.boot] = sample.seq
    return ordered


def inspect_capture(path, source):
    counts = Counter()
    other = Counter()
    digest = sha256()
    with Path(path).open("rb") as stream:
        def lines():
            while line := stream.readline(4097):
                if len(line) > 4096:
                    raise ValueError("capture line exceeds the pilot limit")
                digest.update(line)
                yield line.decode("utf-8-sig", errors="replace")

        for record in read_records(lines(), source):
            if record["type"] in {"frame", "remote_frame", "error_frame"}:
                counts[(record["channel"], record["can_id"], record["format"], record["type"])] += 1
            else:
                other[record["type"]] += 1
    return {
        "format": source,
        "sha256": digest.hexdigest(),
        "frame_count": counts.total(),
        "issues": dict(sorted(other.items())),
        "inventory": [
            {"channel": bus, "id_hex": can_id, "format": protocol, "kind": kind, "count": count}
            for (bus, can_id, protocol, kind), count in sorted(counts.items())
        ],
    }
