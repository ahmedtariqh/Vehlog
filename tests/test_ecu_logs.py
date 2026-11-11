import json
from pathlib import Path
import tempfile
import unittest

from ecu_logs import inspect_diagnostics


class EcuLogTest(unittest.TestCase):
    def test_raw_only_scaled_and_unfinished_records_stay_distinct(self):
        row = {"v": 1, "record": "diag", "service": 1, "ecu": 2024,
               "extended": False, "pid": 12, "status": "data", "mono_us": 15000,
               "bitmap": 0, "nrc": 0, "unit": "rpm", "value_milli": 2000250,
               "pdu": "410C1F41"}
        unknown = {**row, "ecu": 2025, "pid": 47, "unit": "", "value_milli": 0, "pdu": "412FAA"}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "scan.ndjson"
            prefix = "".join(json.dumps(item) + "\n" for item in (row, unknown))
            path.write_text(prefix)
            partial = inspect_diagnostics(path)
            self.assertIsNone(partial["scan_end"])
            self.assertEqual(partial["ecus"][0]["observations"][0]["metric"]["value_milli"], 2000250)
            self.assertIsNone(partial["ecus"][1]["observations"][0]["metric"])
            self.assertEqual(partial["ecus"][1]["observations"][0]["pdu"], "412FAA")
            end = json.dumps({"v": 1, "record": "scan_end", "status_code": 0, "stopped": True}) + "\n"
            path.write_text(prefix + end)
            self.assertEqual(inspect_diagnostics(path)["scan_end"], {"status_code": 0, "stopped": True})
            path.write_text(prefix + end + prefix)
            with self.assertRaisesRegex(ValueError, "must end"):
                inspect_diagnostics(path)
