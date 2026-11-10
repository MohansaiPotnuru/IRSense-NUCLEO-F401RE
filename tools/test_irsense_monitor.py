"""Unit tests for the record parser and dashboard (no serial port needed).

    python -m unittest discover -s tools -p "test_*.py"
"""
import io
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from irsense_monitor import CsvLogger, Dashboard, parse_record  # noqa: E402


class ParseRecordTest(unittest.TestCase):
    def test_object_record(self):
        r = parse_record("$OBJ,1234,7,5000,20000,42\r\n")
        self.assertIsNotNone(r)
        self.assertEqual(r.kind, "OBJ")
        self.assertEqual(r.t_ms, 1234)
        self.assertEqual(r.fields, {"seq": 7, "width_us": 5000, "gap_us": 20000, "total": 42})

    def test_text_fields(self):
        r = parse_record("$ALARM,99,SET,JAM|RATE")
        self.assertEqual(r.fields, {"action": "SET", "flags": "JAM|RATE"})
        self.assertEqual(parse_record("$MODE,1,guard").fields["mode"], "guard")

    def test_record_after_prompt(self):
        self.assertEqual(parse_record("irsense> $BEAM,10,1").fields["blocked"], 1)

    def test_rejects_non_records(self):
        for line in ["[    1.000] I sys: hello", "", "$OBJ,1,2", "$NOPE,1", "$OBJ,x,1,2,3,4",
                     "$RESET"]:
            self.assertIsNone(parse_record(line), line)
        self.assertIsNotNone(parse_record("$RESET,5"))


class DashboardTest(unittest.TestCase):
    def feed(self, dash, *lines):
        for line in lines:
            dash.update(parse_record(line))

    def test_counts_rate_and_alarms(self):
        d = Dashboard()
        self.feed(d, "$MODE,0,monitor",
                  "$OBJ,1000,1,100,0,1", "$OBJ,2000,2,300,1000000,2",
                  "$OBJ,61500,3,200,59500000,3",
                  "$REJ,71000,5", "$ALARM,72000,SET,JAM", "$ALARM,73000,SET,RATE",
                  "$ALARM,74000,CLEAR,JAM")
        self.assertEqual(d.total, 3)
        self.assertEqual(d.rejected, 1)
        self.assertEqual(d.mode, "monitor")
        self.assertEqual(d.alarms, ["RATE"])
        self.assertEqual(d.rate_per_min(), 2)  # only 2000 and 61500 are within 60 s
        self.assertIn("width(min/avg/max)=100/200/300us", d.summary())

    def test_reset(self):
        d = Dashboard()
        self.feed(d, "$OBJ,1,1,100,0,1", "$BATCH,2,1,1", "$RESET,3")
        self.assertEqual((d.total, d.batches, d.rate_per_min()), (0, 0, 0))


class CsvLoggerTest(unittest.TestCase):
    def test_writes_header_and_rows(self):
        out = io.StringIO()
        log = CsvLogger(out)
        log.write(parse_record("$OBJ,5,1,2,3,4"))
        log.write(parse_record("$MODE,6,guard"))
        lines = out.getvalue().strip().splitlines()
        self.assertEqual(lines[0], ",".join(CsvLogger.COLUMNS))
        self.assertIn(",OBJ,5,1,2,3,4,", lines[1])
        self.assertTrue(lines[2].endswith("mode=guard"))


if __name__ == "__main__":
    unittest.main()
