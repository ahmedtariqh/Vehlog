"""Vehlog pilot command-line entry point."""

import argparse
import json
from pathlib import Path

from ecu_logs import inspect_diagnostics
from elm_link import add_arguments as add_elm_arguments, run_elm
from j1939 import run_j1939
from logs import inspect_capture, read_samples
from pid_catalog import Catalog
from rules import Rental, evaluate


def main():
    parser = argparse.ArgumentParser(description="Read device logs and replay a Vehlog rental.")
    commands = parser.add_subparsers(dest="command", required=True)
    replay = commands.add_parser("replay", help="build a standalone rental replay dashboard")
    replay.add_argument("log", type=Path)
    replay.add_argument("rental", type=Path)
    replay.add_argument("--html", type=Path, default=Path("rental-replay.html"))
    replay.add_argument("--json", type=Path, help="also write the filtered replay and alerts as JSON")
    capture = commands.add_parser("inspect-capture", help="inventory a compact candump or ELM log")
    capture.add_argument("log", type=Path)
    capture.add_argument("--source", choices=("candump", "elm"), default="candump")
    diagnostics = commands.add_parser("inspect-diagnostics", help="read an ESP32 bench diagnostic log")
    diagnostics.add_argument("log", type=Path)
    diagnostics.add_argument("--catalog", type=Path, help="decode additional fields from a versioned catalog")
    elm = commands.add_parser("elm", help="acquire read-only data with a physical ELM327")
    add_elm_arguments(elm)
    j1939 = commands.add_parser("inspect-j1939", help="decode Classical J1939 from candump")
    j1939.add_argument("log", type=Path)
    args = parser.parse_args()
    try:
        if args.command == "inspect-j1939":
            return run_j1939(args.log)
        if args.command == "elm":
            return run_elm(args)
        if args.command == "inspect-capture":
            result = inspect_capture(args.log, args.source)
            print(json.dumps(result, indent=2))
            return 2 if {"capture_overrun", "unparsed"} & result["issues"].keys() else 0
        if args.command == "inspect-diagnostics":
            catalog = Catalog.load(args.catalog) if args.catalog else None
            result = inspect_diagnostics(args.log, catalog)
            print(json.dumps(result, indent=2))
            end = result["scan_end"]
            return 0 if end == {"status_code": 0, "stopped": True} else 2
        outputs = [args.html] + ([args.json] if args.json else [])
        output_paths = {path.resolve() for path in outputs}
        if len(output_paths) != len(outputs) or output_paths & {args.log.resolve(), args.rental.resolve()}:
            raise ValueError("outputs must be distinct and must not overwrite the inputs")
        rental = Rental.from_json(json.loads(args.rental.read_text(encoding="utf-8")))
        result = evaluate(rental, read_samples(args.log, rental.device))
        data = json.dumps(result, separators=(",", ":"), allow_nan=False)
        template = Path(__file__).with_name("dashboard.html").read_text(encoding="utf-8")
        args.html.write_text(template.replace("__VEHLOG_DATA__", data.replace("<", "\\u003c")),
                             encoding="utf-8")
        if args.json:
            args.json.write_text(data + "\n", encoding="utf-8")
        print(f"{len(result['samples'])} rental samples, {len(result['alerts'])} events. Wrote {args.html}.")
    except (OSError, ValueError, RecursionError) as error:
        parser.exit(2, f"vehlog: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
