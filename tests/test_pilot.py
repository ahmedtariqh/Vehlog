from dataclasses import replace
from datetime import timedelta
from hashlib import sha256
import json
from pathlib import Path
import tempfile
import unittest

from capture_reader import parse_candump, parse_elm
from logs import Fix, Sample, inspect_capture, read_samples
from rules import Rental, evaluate

EXAMPLES = Path(__file__).resolve().parents[1] / "examples"


class PilotTest(unittest.TestCase):
    def setUp(self):
        self.rental = Rental.from_json(json.loads((EXAMPLES / "rental.json").read_text()))

    def sample(self, seconds, seq, speed=90, accuracy=5, boot="a"):
        return Sample(self.rental.device, boot, seq, self.rental.start + timedelta(seconds=seconds),
                      Fix(30.15, 31.65, accuracy), speed, "vehicle")

    def test_example_events_and_rental_scope(self):
        rows = read_samples(EXAMPLES / "tracker.ndjson", self.rental.device)
        result = evaluate(self.rental, rows)
        self.assertEqual(len(rows), 19)
        self.assertEqual(len(result["samples"]), 17)
        self.assertEqual(result["samples"][0]["seq"], 1)
        self.assertEqual(result["samples"][-1]["seq"], 17)
        self.assertEqual([event["kind"] for event in result["alerts"]],
                         ["speed", "mileage", "zone", "gap", "power", "return"])
        self.assertEqual(result["gaps"], 1)
        self.assertEqual(result["samples"][-1]["distance_km"], 0.694)

    def test_out_of_order_conflicting_retransmission(self):
        rows = (EXAMPLES / "tracker.ndjson").read_text().splitlines()
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "tracker.ndjson"
            path.write_text("\n".join(reversed(rows)))
            samples = read_samples(path, self.rental.device)
            self.assertEqual([sample.seq for sample in samples], list(range(19)))
            conflicting = json.loads(rows[2])
            conflicting["speed_kph"] += 1
            path.write_text("\n".join(rows + [json.dumps(conflicting)]))
            with self.assertRaisesRegex(ValueError, "conflicting samples"):
                read_samples(path, self.rental.device)

    def test_speed_evidence_resets_on_gap_lost_sequence_reboot_or_bad_fix(self):
        sequences = [
            [self.sample(0, 0), self.sample(4, 1), self.sample(17, 2), self.sample(21, 3)],
            [self.sample(0, 0), self.sample(4, 1), self.sample(8, 3), self.sample(12, 4)],
            [self.sample(0, 0), self.sample(4, 1), self.sample(8, 0, boot="b"),
             self.sample(12, 1, boot="b")],
            [self.sample(0, 0), self.sample(4, 1, accuracy=50), self.sample(8, 2), self.sample(12, 3)],
        ]
        for rows in sequences:
            with self.subTest(rows=rows):
                self.assertNotIn("speed", {event["kind"] for event in evaluate(self.rental, rows)["alerts"]})

    def test_return_deadline_uses_booking_state_even_without_telemetry(self):
        at_due = replace(self.rental, returned=None, observed_until=self.rental.due)
        result = evaluate(at_due, [])
        self.assertEqual([event["kind"] for event in result["alerts"]], ["return"])
        result = evaluate(replace(at_due, returned=self.rental.due), [])
        self.assertEqual(result["alerts"], [])

    def test_can_and_elm_inventory_and_payload_export(self):
        path = EXAMPLES / "bench-can.log"
        report = inspect_capture(path, "candump")
        self.assertEqual(report["sha256"], sha256(path.read_bytes()).hexdigest())
        self.assertEqual(report["frame_count"], 6)
        self.assertEqual(report["issues"], {})
        sample_fd = parse_candump("(1794204000.102000) can1 321##50102030405060708090A0B0C R")
        self.assertEqual((sample_fd["fd_flags"], sample_fd["direction"]), (5, "RX"))
        self.assertEqual(parse_elm("18 DA F1 10 04 41")["can_id"], "18DAF110")
        self.assertEqual(inspect_capture(EXAMPLES / "elm-monitor.log", "elm")["issues"]["capture_overrun"], 1)


if __name__ == "__main__":
    unittest.main()
