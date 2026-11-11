"""Evaluate one rental against timestamped, offline device evidence."""

from dataclasses import dataclass
from datetime import datetime
from math import asin, cos, radians, sin, sqrt

from logs import Fix, identifier, number, timestamp

MAX_ACCURACY_M = 30
MAX_GAP_S = 10


@dataclass(frozen=True)
class Rental:
    id: str
    device: str
    start: datetime
    due: datetime
    observed_until: datetime
    returned: datetime | None
    speed_kph: float
    speed_tolerance_kph: float
    speed_seconds: float
    mileage_km: float
    zone_center: Fix
    zone_radius_m: float
    zone_seconds: float

    @classmethod
    def from_json(cls, row):
        if not isinstance(row, dict):
            raise ValueError("rental configuration must be an object")
        start, due, observed = (timestamp(row.get(key)) for key in ("start", "due", "observed_until"))
        returned = None if row.get("returned") is None else timestamp(row["returned"])
        if due <= start or observed < start or (returned and not start < returned <= observed):
            raise ValueError("invalid rental timestamp order")
        zone = row.get("zone")
        if not isinstance(zone, dict):
            raise ValueError("zone must contain lat, lon, radius_m and seconds")
        return cls(
            identifier(row.get("id"), "rental ID"),
            identifier(row.get("device"), "device"), start, due, observed, returned,
            number(row.get("speed_kph"), "speed_kph", 5, 300),
            number(row.get("speed_tolerance_kph"), "speed_tolerance_kph", 0, 30),
            number(row.get("speed_seconds"), "speed_seconds", 1, 120),
            number(row.get("mileage_km"), "mileage_km", 0.01, 10000),
            Fix(number(zone.get("lat"), "zone.lat", -90, 90),
                number(zone.get("lon"), "zone.lon", -180, 180), 0),
            number(zone.get("radius_m"), "zone.radius_m", 50, 2_000_000),
            number(zone.get("seconds"), "zone.seconds", 1, 120),
        )


def distance_m(a, b):
    lat_a, lat_b = radians(a.lat), radians(b.lat)
    term = sin((lat_b - lat_a) / 2)**2
    term += cos(lat_a) * cos(lat_b) * sin(radians(b.lon - a.lon) / 2)**2
    return 12_742_000 * asin(sqrt(min(1, term)))


@dataclass
class Breach:
    since: datetime | None = None
    active: bool = False

    def update(self, time, outside, recovered, seconds):
        if not outside:
            self.since = None
            if recovered:
                self.active = False
            return False
        if self.active:
            return False
        if self.since is None:
            self.since = time
        if (time - self.since).total_seconds() < seconds:
            return False
        self.active = True
        return True


def evaluate(rental, samples):
    cutoff = rental.returned or rental.observed_until
    rows, alerts = [], []
    previous = None
    power = None
    speed_rule, zone_rule = Breach(), Breach()
    distance = 0.0
    gaps = 0
    mileage_notified = False

    def alert(kind, when, message):
        alerts.append({"kind": kind, "time": when.isoformat(), "message": message})

    for sample in samples:
        if sample.time < rental.start or sample.time > cutoff:
            continue
        if rental.returned and sample.time == cutoff:
            continue
        gap_s = (sample.time - (previous.time if previous else rental.start)).total_seconds()
        continuous = (previous is not None and previous.boot == sample.boot
                      and sample.seq == previous.seq + 1 and gap_s <= MAX_GAP_S)
        if not continuous:
            speed_rule.since = zone_rule.since = None
        if gap_s > MAX_GAP_S:
            gaps += 1
            alert("gap", sample.time, f"Telemetry resumed after a {gap_s:g}s gap.")
        fix = sample.fix
        good = fix is not None and fix.accuracy_m <= MAX_ACCURACY_M
        speed = sample.speed_kph if good else None
        if speed_rule.update(sample.time, speed is not None and
                             speed > rental.speed_kph + rental.speed_tolerance_kph,
                             speed is not None and speed <= rental.speed_kph, rental.speed_seconds):
            alert("speed", sample.time, f"{speed:g} km/h after a sustained exceedance of the configured speed.")
        if good:
            radius = distance_m(fix, rental.zone_center)
            if zone_rule.update(sample.time, radius - fix.accuracy_m > rental.zone_radius_m,
                                radius + fix.accuracy_m < rental.zone_radius_m, rental.zone_seconds):
                alert("zone", sample.time, "Vehicle remained outside the configured rental area.")
        else:
            zone_rule.update(sample.time, False, False, rental.zone_seconds)

        if good and continuous and previous.fix and previous.fix.accuracy_m <= MAX_ACCURACY_M:
            segment = distance_m(previous.fix, fix)
            jitter = min(previous.fix.accuracy_m, fix.accuracy_m)
            if jitter < segment <= gap_s * (300 / 3.6) + previous.fix.accuracy_m + fix.accuracy_m:
                distance += segment
        if distance >= rental.mileage_km * 1000 and not mileage_notified:
            mileage_notified = True
            alert("mileage", sample.time, f"Estimated observed distance reached {rental.mileage_km:g} km.")
        if sample.power == "backup" and power != "backup":
            alert("power", sample.time, "Device reported backup power. Vehicle power loss is possible.")
        if sample.power is not None:
            power = sample.power

        row = sample.json()
        row.update({"fix": row["fix"] if good else None, "speed_kph": speed,
                    "distance_km": round(distance / 1000, 3), "break_before": not continuous})
        rows.append(row)
        previous = sample

    if cutoff >= rental.due and rental.returned != rental.due:
        alert("return", rental.due, "Rental passed its scheduled return time without a recorded check-in.")
    return {
        "rental": {
            "id": rental.id, "device": rental.device, "start": rental.start.isoformat(),
            "due": rental.due.isoformat(), "observed_until": rental.observed_until.isoformat(),
            "returned": rental.returned.isoformat() if rental.returned else None,
            "speed_kph": rental.speed_kph, "mileage_km": rental.mileage_km,
            "zone": {"lat": rental.zone_center.lat, "lon": rental.zone_center.lon,
                     "radius_m": rental.zone_radius_m},
        },
        "gaps": gaps,
        "samples": rows,
        "alerts": sorted(alerts, key=lambda item: item["time"]),
    }
