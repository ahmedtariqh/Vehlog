"""Decode known fields from preserved OBD payloads using explicit, revisioned data."""

from dataclasses import dataclass
from fractions import Fraction
import json

SERVICES = frozenset({1, 2, 6, 9})


def bounded_int(value, lower, upper, name):
    if type(value) is not int or not lower <= value <= upper:
        raise ValueError(f"invalid PID catalog {name}")
    return value


def rational(value, name):
    if not isinstance(value, list) or len(value) != 2:
        raise ValueError(f"PID catalog {name} must be [numerator, denominator]")
    return Fraction(bounded_int(value[0], -10**9, 10**9, name),
                    bounded_int(value[1], 1, 10**9, name))


@dataclass(frozen=True)
class Field:
    name: str
    unit: str
    offset: int
    width: int
    order: str
    signed: bool
    mask: int
    shift: int
    scale: Fraction
    bias: Fraction
    invalid: frozenset

    @classmethod
    def parse(cls, spec):
        name, unit = spec.get("name"), spec.get("unit")
        if not all(isinstance(item, str) and 0 < len(item) <= 80 for item in (name, unit)):
            raise ValueError("invalid PID catalog field name or unit")
        width = bounded_int(spec.get("width"), 1, 8, "width")
        signed = spec.get("signed", False)
        order = spec.get("order", "big")
        if type(signed) is not bool or order not in ("big", "little"):
            raise ValueError("invalid PID catalog byte order or signed flag")
        mask = bounded_int(spec.get("mask", (1 << (8 * width)) - 1), 1,
                           (1 << (8 * width)) - 1, "mask")
        shift = bounded_int(spec.get("shift", 0), 0, (8 * width) - 1, "shift")
        if mask >> shift == 0 or (signed and (shift or mask != (1 << (8 * width)) - 1)):
            raise ValueError("invalid PID catalog mask/shift for field")
        invalid = spec.get("invalid", [])
        if not isinstance(invalid, list) or len(invalid) > 64:
            raise ValueError("invalid PID catalog unavailable values")
        low = -(1 << ((8 * width) - 1)) if signed else 0
        high = (1 << ((8 * width) - 1)) - 1 if signed else mask >> shift
        offset = bounded_int(spec.get("offset"), 2, 4095 - width, "offset")
        return cls(name, unit, offset, width, order, signed, mask, shift,
                   rational(spec.get("scale", [1, 1]), "scale"),
                   rational(spec.get("bias", [0, 1]), "bias"),
                   frozenset(bounded_int(item, low, high, "invalid value") for item in invalid))

    def decode(self, pdu):
        data = pdu[self.offset:self.offset + self.width]
        if len(data) != self.width:
            return {"name": self.name, "unit": self.unit, "status": "truncated"}
        raw = int.from_bytes(data, self.order, signed=self.signed)
        if not self.signed:
            raw = (raw & self.mask) >> self.shift
        if raw in self.invalid:
            return {"name": self.name, "unit": self.unit, "raw": raw, "status": "unavailable"}
        value = self.scale * raw + self.bias
        return {"name": self.name, "unit": self.unit, "raw": raw, "status": "decoded",
                "numerator": value.numerator, "denominator": value.denominator}


class Catalog:
    def __init__(self, revision, entries):
        self.revision = revision
        self.entries = entries

    @classmethod
    def load(cls, path):
        with path.open("rb") as stream:
            raw = stream.read(2 * 1024 * 1024 + 1)
        if len(raw) > 2 * 1024 * 1024:
            raise ValueError("PID catalog exceeds 2 MiB")
        doc = json.loads(raw)
        if not isinstance(doc, dict) or type(doc.get("version")) is not int or doc["version"] != 1:
            raise ValueError("unsupported PID catalog version")
        revision = doc.get("revision")
        metrics = doc.get("metrics")
        if (not isinstance(revision, str) or not 0 < len(revision) <= 120 or
                not isinstance(metrics, list) or len(metrics) > 4096):
            raise ValueError("invalid PID catalog metadata")
        entries = {}
        seen = set()
        for spec in metrics:
            if not isinstance(spec, dict):
                raise ValueError("invalid PID catalog record")
            service = bounded_int(spec.get("service"), 1, 9, "service")
            if service not in SERVICES:
                raise ValueError("unsupported PID catalog service")
            selector = bounded_int(spec.get("selector"), 0, 255, "selector")
            field = Field.parse(spec)
            key = service, selector
            if (*key, field.name) in seen:
                raise ValueError("duplicate PID catalog field")
            if service == 2 and field.offset < 3:
                raise ValueError("freeze-frame fields must follow the frame number")
            seen.add((*key, field.name))
            entries.setdefault(key, []).append(field)
        return cls(revision, entries)

    def decode(self, service, selector, pdu):
        if len(pdu) < 2 or pdu[:2] != bytes((service + 0x40, selector)):
            return []
        return [field.decode(pdu) for field in self.entries.get((service, selector), ())]
