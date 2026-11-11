import argparse
from base64 import b64decode
from io import StringIO
import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from capture_reader import read_records
from ecu_logs import decode_identified_reply, inspect_diagnostics, parse_diagnostic
from pid_catalog import Catalog
from elm_link import ElmLink, acquire, add_arguments, request_plan
from j1939 import captured_messages


def elm_args(*args):
    parser = argparse.ArgumentParser()
    add_arguments(parser)
    return parser.parse_args(("fixture", *args))


class FakeAdapter:
    def __init__(self):
        self.output = b""
        self.commands = []
        self.clock = 0.0

    def write(self, data):
        self.commands.append(data)
        responses = {
            b"ATZ\r": b"\rELM327 v2.3\r>",
            b"0902\r": b"\r014\r0: 49 02 01 31 32 33\r>",
            b"0700\r": b"\r?\r>",
            b"ATMA\r": b"7E8 8 01 02 03 04\rBUFFER FULL\r>",
        }
        self.output = responses.get(data, b"\rOK\r>")
        return len(data)

    def read(self, _count):
        self.clock += 0.01
        data, self.output = self.output[:3], self.output[3:]
        return data


class CompletionTest(unittest.TestCase):
    def test_bridge_preserves_fragmented_serial_and_validation(self):
        device = FakeAdapter()
        observed = []
        ok = acquire(ElmLink(device, clock=lambda: device.clock),
                     elm_args("--protocol", "6", "--request", "09 02"), observed.append)
        self.assertTrue(ok)
        reply, = (entry for entry in observed if entry["kind"] == "response")
        self.assertEqual(b64decode(reply["raw_b64"]), b"\r014\r0: 49 02 01 31 32 33\r")
        self.assertIn(b"ATTP6\r", device.commands)
        self.assertIn(b"ATRV\r", device.commands)
        for args in (("--request", "04"), ("--bench", "--request", "08 01"),
                     ("--request", "220102"), ("--protocol", "0", "--duration", "1"),
                     ("--request", "010C\r04")):
            with self.subTest(args=args), self.assertRaises(ValueError):
                request_plan(elm_args(*args))
        settings, requests = request_plan(elm_args("--protocol", "B", "--bench", "--user1", "8004",
                                                    "--tx-header", "7E0", "--request", "0100"))
        self.assertLess(settings.index("ATPB8004"), settings.index("ATTPB"))
        self.assertEqual(requests, ["0100"])
        self.assertEqual(request_plan(elm_args("--protocol", "3", "--request", "050001"))[1],
                         ["050001"])

    def test_monitor_detects_early_overrun_even_across_serial_chunks(self):
        device = FakeAdapter()
        observed = []
        ok = acquire(ElmLink(device, clock=lambda: device.clock),
                     elm_args("--protocol", "6", "--duration", "1"), observed.append)
        self.assertFalse(ok)
        self.assertIn(b"ATCSM1\r", device.commands)
        self.assertIn(b"ATD1\r", device.commands)
        self.assertIn(b"BUFFER FULL", b"".join(
            b64decode(record["raw_b64"]) for record in observed if record["kind"] == "monitor_chunk"
        ))

    def test_j1939_bam_reassembles_and_preserves_source(self):
        capture = StringIO("""(0.000000) can0 18ECFF08#200A0002FFCAFE00
(0.020000) can0 18ECFF09#200A0002FFCAFE00
(0.060000) can0 18EBFF08#0100003422050201
(0.070000) can0 18EBFF09#0100003422050201
(0.110000) can0 18EBFF08#02000201FFFFFFFF
DROPCOUNT: 1
""")
        records = list(captured_messages(read_records(capture, "candump")))
        message, = (item for item in records if item["kind"] == "message")
        partial, = (item for item in records if item["kind"] == "incomplete")
        self.assertEqual((message["source"], message["pgn"], message["transport"]), (8, 0xFECA, "bam"))
        self.assertEqual(message["data_hex"], "00003422050201000201")
        self.assertEqual(message["dtcs"][0]["spn"], 0x2234)
        self.assertEqual(partial["source"], 9)
        self.assertNotIn("dtcs", partial)

    def test_diagnostic_service_decoding_keeps_raw_replies(self):
        self.assertEqual(decode_identified_reply(3, 0, bytes.fromhex("43010133")),
                         {"dtc_category": "stored", "dtcs": ["P0133"]})
        self.assertEqual(decode_identified_reply(10, 0, bytes.fromhex("4A00")),
                         {"dtc_category": "permanent", "dtcs": []})
        vin = b"1HGCM82633A004352"
        self.assertEqual(decode_identified_reply(9, 2, b"\x49\x02\x01" + vin), {"vin": vin.decode()})
        row = {"service": 6, "extended": False, "ecu": 2024, "pid": 1, "bitmap": 0,
               "nrc": 0, "mono_us": 10, "value_milli": 0, "status": "data", "unit": "",
               "pdu": "4601" + ("AA" * 4093)}
        self.assertEqual(parse_diagnostic(row)["pdu"], row["pdu"])

    def test_maximum_j1939_transport_payload(self):
        data = bytes(index % 256 for index in range(1785))
        lines = ["(0.000000) can0 18ECFF08#20F906FFFFEEFE00"]
        for i in range(255):
            part = data[i * 7:(i + 1) * 7]
            lines.append(f"({(i + 1) * 0.05:.6f}) can0 18EBFF08#{i + 1:02X}{part.hex()}")
        results = list(captured_messages(read_records(lines, "candump")))
        self.assertEqual([record["kind"] for record in results], ["transport_control", "message"])
        self.assertEqual(results[1]["pgn"], 0xFEEE)
        self.assertEqual(bytes.fromhex(results[1]["data_hex"]), data)

    def test_catalog_and_full_length_file_input(self):
        catalog = Catalog.load(Path(__file__).parents[1] / "examples" / "pid-catalog.json")
        voltage = catalog.decode(1, 66, bytes.fromhex("414236B0"))
        self.assertEqual((voltage[0]["numerator"], voltage[0]["denominator"]), (14, 1))
        self.assertEqual(catalog.decode(1, 66, bytes.fromhex("4142"))[0]["status"], "truncated")
        self.assertEqual(catalog.decode(1, 66, bytes.fromhex("414336B0")), [])
        row = {"v": 1, "record": "diag", "service": 2, "extended": False, "ecu": 2024,
               "pid": 1, "freeze_frame": 4, "bitmap": 0, "nrc": 0, "mono_us": 10,
               "value_milli": 0, "status": "data", "unit": "", "pdu": "420104" + "AA" * 4092}
        with TemporaryDirectory() as folder:
            path = Path(folder) / "scan.ndjson"
            path.write_text(json.dumps(row) + "\n")
            output = inspect_diagnostics(path, catalog)
        record = output["ecus"][0]["observations"][0]
        self.assertEqual((record["service"], record["freeze_frame"]), (2, 4))
        self.assertEqual(record["pdu"], row["pdu"])
