"""Independent package, financial arithmetic, and launcher preflight checks."""
import hashlib
import importlib.util
import json
import subprocess
import tempfile
import unittest
from decimal import Decimal, ROUND_HALF_UP
from pathlib import Path
from xml.etree import ElementTree as ET
from zipfile import ZipFile

WORKSPACE = Path(__file__).resolve().parents[2]
SCRIPTS = WORKSPACE / "TutorialAutomation" / "scripts"
REFERENCES = WORKSPACE / "References" / "excel-simulation"
NS = {"s": "http://schemas.openxmlformats.org/spreadsheetml/2006/main"}


class ExcelSimulationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        (WORKSPACE / ".tmp").mkdir(exist_ok=True)
        cls.manifest = json.loads((REFERENCES / "manifest.json").read_text(encoding="utf-8"))

    def test_ten_complete_packages_match_manifest(self):
        self.assertEqual(10, len(list(REFERENCES.glob("*.xlsx"))))
        self.assertEqual(10, len(self.manifest["workbooks"]))
        self.assertEqual(10, len({m["company"] for m in self.manifest["workbooks"]}))
        for model in self.manifest["workbooks"]:
            path = REFERENCES / model["file"]
            self.assertEqual(model["sha256"], hashlib.sha256(path.read_bytes()).hexdigest())
            with ZipFile(path) as archive:
                self.assertIsNone(archive.testzip())
                for name in archive.namelist():
                    ET.fromstring(archive.read(name))
                workbook = ET.fromstring(archive.read("xl/workbook.xml"))
                self.assertEqual(["Summary", "Price Checks"], [s.get("name") for s in workbook.findall("s:sheets/s:sheet", NS)])
                for index, sheet in enumerate(model["sheets"], 1):
                    root = ET.fromstring(archive.read(f"xl/worksheets/sheet{index}.xml"))
                    cells = {c.get("r"): c for c in root.findall("s:sheetData/s:row/s:c", NS)}
                    expected_refs = set()
                    for r, row in enumerate(sheet["rows"], 1):
                        for c, expected in enumerate(row, 1):
                            if expected is None or expected == "":
                                continue
                            ref = f"{chr(64 + c)}{r}"
                            expected_refs.add(ref)
                            cell = cells[ref]
                            if isinstance(expected, dict):
                                self.assertEqual(expected["formula"][1:], cell.findtext("s:f", namespaces=NS))
                                actual = cell.findtext("s:v", default="", namespaces=NS)
                                if isinstance(expected["cached"], str):
                                    self.assertEqual("str", cell.get("t"))
                                    self.assertEqual(expected["cached"], actual)
                                else:
                                    self.assertAlmostEqual(expected["cached"], float(actual), places=7)
                            elif isinstance(expected, str):
                                self.assertEqual("inlineStr", cell.get("t"))
                                self.assertEqual(expected, cell.findtext("s:is/s:t", namespaces=NS))
                            else:
                                self.assertAlmostEqual(expected, float(cell.findtext("s:v", namespaces=NS)))
                    self.assertEqual(expected_refs, set(cells))
                    self.assertIn("SIMULATED DATA", cells["A2"].findtext("s:is/s:t", namespaces=NS))
                    self.assertEqual("frozen", root.find("s:sheetViews/s:sheetView/s:pane", NS).get("state"))
                    if sheet["filter"]:
                        self.assertEqual("A4:K28", root.find("s:autoFilter", NS).get("ref"))
                        self.assertEqual(4, len(root.findall("s:conditionalFormatting/s:cfRule", NS)))

    def test_quote_outcomes_and_summary_arithmetic(self):
        for model in self.manifest["workbooks"]:
            checks = model["sheets"][1]["rows"][4:]
            self.assertEqual(24, len(checks))
            gaps, outcomes, broker_total, supplier_total = [], [], Decimal(0), Decimal(0)
            for row in checks:
                supplier = Decimal(str(row[5]))
                supplier_total += supplier
                if isinstance(row[4], str):
                    gap, outcome = "", "REVIEW"
                else:
                    broker = Decimal(str(row[4]))
                    broker_total += broker
                    gap = (broker - supplier).quantize(Decimal("0.01"), rounding=ROUND_HALF_UP)
                    gaps.append(gap)
                    outcome = "EQUAL" if not gap else "BROKER HIGHER" if gap > 0 else "BROKER LOWER"
                self.assertEqual(gap if gap == "" else float(gap), row[6]["cached"])
                self.assertEqual(outcome, row[8]["cached"])
                outcomes.append(outcome)
            summary = {row[0]: row[1] for row in model["sheets"][0]["rows"][4:]}
            self.assertEqual(22, summary["Comparable quotes"]["cached"])
            for label, result in [("Equal", "EQUAL"), ("Broker lower", "BROKER LOWER"), ("Broker higher", "BROKER HIGHER"), ("Review", "REVIEW")]:
                self.assertEqual(outcomes.count(result), summary[label]["cached"])
                self.assertGreater(outcomes.count(result), 0)
            self.assertAlmostEqual(float(sum(gaps) / len(gaps)), summary["Average gap (A$)"]["cached"])
            self.assertEqual(float(max(map(abs, gaps))), summary["Largest absolute gap (A$)"]["cached"])
            self.assertEqual(float(broker_total), summary["Available broker total (A$)"]["cached"])
            self.assertEqual(float(supplier_total), summary["Supplier total (A$)"]["cached"])

    def test_rebuild_is_deterministic_and_preserves_existing_files(self):
        spec = importlib.util.spec_from_file_location("excel_generator", SCRIPTS / "New-ExcelSimulation.py")
        generator = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(generator)
        with tempfile.TemporaryDirectory() as directory:
            rebuilt = generator.build(Path(directory))
            self.assertEqual([m["sha256"] for m in self.manifest["workbooks"]], [m["sha256"] for m in rebuilt])
            with self.assertRaises(FileExistsError):
                generator.build(Path(directory))

    def run_preflight(self, config):
        return subprocess.run(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                               str(SCRIPTS / "Start-ExcelReference.ps1"), "-Config", str(config), "-ValidateOnly"],
                              capture_output=True, text=True, cwd=WORKSPACE)

    def test_preflight_refuses_unowned_outputs_and_changed_sources(self):
        with tempfile.TemporaryDirectory(dir=WORKSPACE / ".tmp") as directory:
            directory = Path(directory)
            job = json.loads((SCRIPTS / "excel-job.json").read_text(encoding="utf-8"))
            job["references"] = str(REFERENCES)
            job["destination"] = str(directory / "output")
            config = directory / "job.json"
            config.write_text(json.dumps(job), encoding="utf-8")
            good = self.run_preflight(config)
            self.assertEqual(0, good.returncode, good.stderr)
            self.assertFalse(Path(job["destination"]).exists())
            output = Path(job["destination"])
            output.mkdir()
            filename = self.manifest["workbooks"][0]["file"]
            dummy = output / filename
            dummy.write_bytes(b"Preserve existing user data")
            rejected = self.run_preflight(config)
            self.assertNotEqual(0, rejected.returncode)
            self.assertIn("not owned", rejected.stderr)
            self.assertEqual(b"Preserve existing user data", dummy.read_bytes())
            state = {"version": 1, "files": [{"file": filename, "sha256": "0" * 64}]}
            (output / ".excel-playback-state.json").write_text(json.dumps(state), encoding="utf-8")
            rejected = self.run_preflight(config)
            self.assertNotEqual(0, rejected.returncode)
            self.assertIn("Source changed", rejected.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
