#!/usr/bin/env python3
"""IRSense serial monitor: live dashboard + CSV logger for the $-record stream.

Connects to the NUCLEO-F401RE virtual COM port, turns on `stream`, parses
the machine-readable records and shows a live summary. Log lines and CLI
output are passed through unchanged.

    pip install pyserial
    python tools/irsense_monitor.py --port COM5               # Windows
    python tools/irsense_monitor.py --port /dev/ttyACM0 --csv run.csv
    python tools/irsense_monitor.py --list                    # find the port

Record formats (see docs/CLI.md):
    $OBJ,<t_ms>,<seq>,<width_us>,<gap_us>,<total>
    $REJ,<t_ms>,<width_us>
    $BEAM,<t_ms>,<0|1>
    $BATCH,<t_ms>,<batches>,<target>
    $ALARM,<t_ms>,<SET|CLEAR>,<FLAG|FLAG...>
    $MODE,<t_ms>,<counter|monitor|guard>
    $PAUSE,<t_ms>,<0|1>
    $GUARD,<t_ms>,<disarmed|arming|armed>
    $RESET,<t_ms>
"""
from __future__ import annotations

import argparse
import csv
import sys
import time
from dataclasses import dataclass, field
from typing import Dict, List, Optional, TextIO

# kind -> names of the fields after the timestamp; int fields are converted.
RECORD_FIELDS: Dict[str, List[str]] = {
    "OBJ": ["seq", "width_us", "gap_us", "total"],
    "REJ": ["width_us"],
    "BEAM": ["blocked"],
    "BATCH": ["batches", "target"],
    "ALARM": ["action", "flags"],
    "MODE": ["mode"],
    "PAUSE": ["paused"],
    "GUARD": ["state"],
    "RESET": [],
}
TEXT_FIELDS = {"action", "flags", "mode", "state"}


@dataclass
class Record:
    kind: str
    t_ms: int
    fields: Dict[str, object]


def parse_record(line: str) -> Optional[Record]:
    """Parses one $-record. Returns None for anything else (logs, prompts)."""
    line = line.strip()
    # The CLI prompt may precede a record on the same line.
    start = line.find("$")
    if start < 0:
        return None
    parts = line[start + 1:].split(",")
    kind = parts[0]
    names = RECORD_FIELDS.get(kind)
    if names is None or len(parts) != len(names) + 2:
        return None
    try:
        t_ms = int(parts[1])
        values: Dict[str, object] = {}
        for name, raw in zip(names, parts[2:]):
            values[name] = raw if name in TEXT_FIELDS else int(raw)
    except ValueError:
        return None
    return Record(kind, t_ms, values)


@dataclass
class Dashboard:
    """Accumulates records into the numbers shown on screen."""
    total: int = 0
    rejected: int = 0
    batches: int = 0
    mode: str = "?"
    blocked: bool = False
    alarms: List[str] = field(default_factory=list)
    widths: List[int] = field(default_factory=list)
    gaps: List[int] = field(default_factory=list)
    obj_times_ms: List[int] = field(default_factory=list)

    def update(self, r: Record) -> None:
        f = r.fields
        if r.kind == "OBJ":
            self.total = int(f["total"])  # type: ignore[arg-type]
            self.widths.append(int(f["width_us"]))  # type: ignore[arg-type]
            if int(f["gap_us"]) > 0:  # type: ignore[arg-type]
                self.gaps.append(int(f["gap_us"]))  # type: ignore[arg-type]
            self.obj_times_ms.append(r.t_ms)
            self.widths = self.widths[-1000:]
            self.gaps = self.gaps[-1000:]
        elif r.kind == "REJ":
            self.rejected += 1
        elif r.kind == "BATCH":
            self.batches = int(f["batches"])  # type: ignore[arg-type]
        elif r.kind == "MODE":
            self.mode = str(f["mode"])
        elif r.kind == "BEAM":
            self.blocked = f["blocked"] == 1
        elif r.kind == "ALARM":
            flags = str(f["flags"]).split("|")
            if f["action"] == "SET":
                self.alarms = sorted(set(self.alarms) | set(flags))
            else:
                self.alarms = [a for a in self.alarms if a not in flags]
        elif r.kind == "RESET":
            self.total = self.rejected = self.batches = 0
            self.widths.clear()
            self.gaps.clear()
            self.obj_times_ms.clear()

    def rate_per_min(self) -> int:
        """Objects in the 60 s before the newest object (device time)."""
        if not self.obj_times_ms:
            return 0
        newest = self.obj_times_ms[-1]
        self.obj_times_ms = [t for t in self.obj_times_ms if newest - t < 60000]
        return len(self.obj_times_ms)

    def summary(self) -> str:
        def stat(values: List[int]) -> str:
            if not values:
                return "-"
            return f"{min(values)}/{sum(values) // len(values)}/{max(values)}"

        alarms = ",".join(self.alarms) if self.alarms else "none"
        return (f"mode={self.mode} beam={'BLOCKED' if self.blocked else 'clear'} "
                f"total={self.total} rej={self.rejected} batches={self.batches} "
                f"rate={self.rate_per_min()}/min width(min/avg/max)={stat(self.widths)}us "
                f"gap={stat(self.gaps)}us alarms={alarms}")


class CsvLogger:
    COLUMNS = ["host_time", "kind", "t_ms", "seq", "width_us", "gap_us", "total", "detail"]

    def __init__(self, stream: TextIO) -> None:
        self._writer = csv.DictWriter(stream, fieldnames=self.COLUMNS, extrasaction="ignore")
        self._writer.writeheader()
        self._stream = stream

    def write(self, r: Record) -> None:
        row: Dict[str, object] = {"host_time": f"{time.time():.3f}", "kind": r.kind,
                                  "t_ms": r.t_ms}
        row.update(r.fields)
        details = [f"{k}={v}" for k, v in r.fields.items() if k not in self.COLUMNS]
        row["detail"] = " ".join(details)
        self._writer.writerow(row)
        self._stream.flush()


def list_ports() -> int:
    from serial.tools import list_ports as lp  # type: ignore[import-not-found]
    for p in lp.comports():
        print(f"{p.device:12} {p.description}")
    return 0


def run(port: str, baud: int, csv_path: Optional[str], quiet: bool) -> int:
    import serial  # type: ignore[import-not-found]

    dash = Dashboard()
    csv_file = open(csv_path, "w", newline="", encoding="utf-8") if csv_path else None
    logger = CsvLogger(csv_file) if csv_file else None
    last_summary = 0.0

    with serial.Serial(port, baud, timeout=0.2) as ser:
        ser.write(b"\r\nstream on\r\nstatus\r\n")
        buf = b""
        try:
            while True:
                buf += ser.read(512)
                while b"\n" in buf:
                    raw, buf = buf.split(b"\n", 1)
                    line = raw.decode("utf-8", errors="replace").rstrip("\r")
                    rec = parse_record(line)
                    if rec is None:
                        if line.strip():
                            print(line)
                        continue
                    dash.update(rec)
                    if logger:
                        logger.write(rec)
                    if not quiet and rec.kind != "BEAM":
                        print(f"  {rec.kind:<6} t={rec.t_ms} {rec.fields}")
                now = time.monotonic()
                if now - last_summary >= 2.0:
                    last_summary = now
                    print(f"[{time.strftime('%H:%M:%S')}] {dash.summary()}")
        except KeyboardInterrupt:
            ser.write(b"stream off\r\n")
        finally:
            if csv_file:
                csv_file.close()
    return 0


def main(argv: Optional[List[str]] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--port", help="serial port, e.g. COM5 or /dev/ttyACM0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--csv", help="append every record to this CSV file")
    ap.add_argument("--quiet", action="store_true", help="only print the periodic summary")
    ap.add_argument("--list", action="store_true", help="list serial ports and exit")
    args = ap.parse_args(argv)

    if args.list:
        return list_ports()
    if not args.port:
        ap.error("--port is required (use --list to find it)")
    return run(args.port, args.baud, args.csv, args.quiet)


if __name__ == "__main__":
    sys.exit(main())
