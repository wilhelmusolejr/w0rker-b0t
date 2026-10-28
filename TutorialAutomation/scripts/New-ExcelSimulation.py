"""Build the ten fictional Excel references and their UI playback manifest.

Uses only Python's standard library. Nothing from the original workbook is
changed; the comparison layout is the reference, while all quotes are invented.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from datetime import date, timedelta
from pathlib import Path
from xml.etree import ElementTree as ET
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT = ROOT / "References" / "excel-simulation"
NS = "http://schemas.openxmlformats.org/spreadsheetml/2006/main"
REL = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
PKG = "http://schemas.openxmlformats.org/package/2006/relationships"
CONTENT = "http://schemas.openxmlformats.org/package/2006/content-types"
NOTICE = "SIMULATED DATA - fictional companies and quotes for a teaching demo. Currency: AUD."
COMPANIES = [
    "Harbour Trail Rentals", "Southern Ridge Campers", "Blue Wattle Motorhomes",
    "Coastal Fern Travel", "Outback Lantern Hire", "Silver Creek Campers",
    "Red Sand Touring", "Alpine Cove Rentals", "Golden Bay Motorhomes",
    "Riverstone Road Trips",
]
HEADERS = [
    "Company", "Route", "Search settings", "Rental dates", "Demo broker price",
    "Supplier price", "Gap (A$)", "Other broker price", "Result", "Remarks / Notes", "Follow-up",
]
ROUTES = ["Sydney to Sydney", "Sydney to Melbourne", "Brisbane to Cairns", "Perth to Perth"]
COVERS = ["Basic", "Standard", "Premium"]
VEHICLES = ["Campervan", "Motorhome"]


def col(number: int) -> str:
    value = ""
    while number:
        number, digit = divmod(number - 1, 26)
        value = chr(65 + digit) + value
    return value


def formula(expression: str, cached: object) -> dict:
    return {"formula": expression, "cached": cached}


def company_model(index: int, company: str) -> dict:
    rows = [[f"{company} - price checks"], [NOTICE], [], HEADERS]
    outcomes, gaps = [], []
    for quote in range(24):
        row = quote + 5
        supplier = round(980 + index * 137.5 + quote * 43.25 + (quote % 3) * 91.2, 2)
        delta = [0, -42.5, 65, -17.25, 120.75, 0][(quote + index) % 6]
        broker = round(supplier + delta, 2)
        other = round(supplier + 25 + (quote % 4) * 13.5, 2)
        unavailable = quote in {8 + index % 3, 20 + index % 3}
        if unavailable:
            broker = "Unavailable"
        gap = "" if unavailable else round(broker - supplier, 2)
        result = "REVIEW" if unavailable else (
            "EQUAL" if gap == 0 else "BROKER HIGHER" if gap > 0 else "BROKER LOWER")
        outcomes.append(result)
        if not unavailable:
            gaps.append(gap)
        start = date(2026, 11, 3) + timedelta(days=(quote // 6) * 14 + index)
        rows.append([
            company, ROUTES[quote // 6],
            f"{VEHICLES[(quote // 3) % 2]} | {COVERS[quote % 3]} | {'<35' if quote < 12 else '>35'} days",
            f"{start.isoformat()} to {(start + timedelta(days=7)).isoformat()}",
            broker, supplier,
            formula(f'=IF(AND(ISNUMBER(E{row}),ISNUMBER(F{row})),ROUND(E{row}-F{row},2),"")', gap),
            other,
            formula(f'=IF(AND(ISNUMBER(E{row}),ISNUMBER(F{row})),IF(G{row}=0,"EQUAL",IF(G{row}>0,"BROKER HIGHER","BROKER LOWER")),"REVIEW")', result),
            "Simulated quote unavailable" if unavailable else "Dummy quote; compare cover",
            "Request sample quote" if unavailable else "Check cover cost" if gap > 0 else "No action",
        ])
    count_range = "'Price Checks'!I5:I28"
    gap_range = "'Price Checks'!G5:G28"
    metrics = [
        ["Company", company], ["Quotes checked", formula("=COUNTA('Price Checks'!A5:A28)", 24)],
        ["Comparable quotes", formula(f"=COUNT({gap_range})", len(gaps))],
    ]
    for label, outcome in [("Equal", "EQUAL"), ("Broker lower", "BROKER LOWER"),
                           ("Broker higher", "BROKER HIGHER"), ("Review", "REVIEW")]:
        metrics.append([label, formula(f'=COUNTIF({count_range},"{outcome}")', outcomes.count(outcome))])
    metrics += [
        ["Average gap (A$)", formula(f"=IFERROR(AVERAGE({gap_range}),0)", sum(gaps) / len(gaps))],
        ["Largest absolute gap (A$)", formula(f"=MAX(MAX({gap_range}),-MIN({gap_range}))", max(map(abs, gaps)))],
        ["Available broker total (A$)", formula("=SUM('Price Checks'!E5:E28)", round(sum(r[4] for r in rows[4:] if isinstance(r[4], (int, float))), 2))],
        ["Supplier total (A$)", formula("=SUM('Price Checks'!F5:F28)", round(sum(r[5] for r in rows[4:]), 2))],
        ["Currency", "AUD"], ["Data source", "Invented for this demo"],
    ]
    return {
        "file": f"{index + 1:02d}_{company.replace(' ', '_')}.xlsx", "company": company,
        "sheets": [
            {"name": "Summary", "widths": [43, 40], "currency_columns": [],
             "currency_rows": [12, 13, 14, 15], "filter": False,
             "rows": [[f"{company} - summary"], [NOTICE], [], ["Metric", "Value"], *metrics]},
            {"name": "Price Checks", "widths": [27, 25, 39, 29, 20, 19, 17, 21, 23, 35, 27],
             "currency_columns": [5, 6, 7, 8], "currency_rows": [], "filter": True, "rows": rows},
        ],
    }


def node(parent: ET.Element, tag: str, attrs=None, text=None) -> ET.Element:
    element = ET.SubElement(parent, f"{{{NS}}}{tag}", attrs or {})
    if text is not None:
        element.text = str(text)
    return element


def xml(element: ET.Element) -> bytes:
    return ET.tostring(element, encoding="utf-8", xml_declaration=True)


def styles_xml() -> bytes:
    root = ET.Element(f"{{{NS}}}styleSheet")
    formats = node(root, "numFmts", {"count": "1"})
    node(formats, "numFmt", {"numFmtId": "164", "formatCode": '#,##0.00;[Red]-#,##0.00'})
    fonts = node(root, "fonts", {"count": "4"})
    for size, color, bold, italic in [(11, "243347", False, False), (17, "FFFFFF", True, False),
                                      (10, "596778", False, True), (11, "FFFFFF", True, False)]:
        font = node(fonts, "font")
        node(font, "sz", {"val": str(size)})
        node(font, "color", {"rgb": "FF" + color})
        node(font, "name", {"val": "Calibri"})
        if bold:
            node(font, "b")
        if italic:
            node(font, "i")
    fills = node(root, "fills", {"count": "5"})
    node(node(fills, "fill"), "patternFill", {"patternType": "none"})
    node(node(fills, "fill"), "patternFill", {"patternType": "gray125"})
    for color in ["18354D", "167D8D", "F0F6FA"]:
        pattern = node(node(fills, "fill"), "patternFill", {"patternType": "solid"})
        node(pattern, "fgColor", {"rgb": "FF" + color})
        node(pattern, "bgColor", {"indexed": "64"})
    borders = node(root, "borders", {"count": "1"})
    border = node(borders, "border")
    for side in ["left", "right", "top", "bottom", "diagonal"]:
        node(border, side)
    node(node(root, "cellStyleXfs", {"count": "1"}), "xf",
         {"numFmtId": "0", "fontId": "0", "fillId": "0", "borderId": "0"})
    xfs = node(root, "cellXfs", {"count": "9"})
    # Default, title, notice, header, odd/even text, odd/even money, summary label.
    for font, fill, fmt in [(0, 0, 0), (1, 2, 0), (2, 0, 0), (3, 3, 0),
                            (0, 0, 0), (0, 4, 0), (0, 0, 164), (0, 4, 164), (0, 4, 0)]:
        xf = node(xfs, "xf", {"numFmtId": str(fmt), "fontId": str(font), "fillId": str(fill),
                               "borderId": "0", "xfId": "0", "applyAlignment": "1"})
        if fmt:
            xf.set("applyNumberFormat", "1")
        node(xf, "alignment", {"vertical": "center", "wrapText": "1"})
    node(node(root, "cellStyles", {"count": "1"}), "cellStyle", {"name": "Normal", "xfId": "0", "builtinId": "0"})
    dxfs = node(root, "dxfs", {"count": "4"})
    for color in ["FCE4E4", "E2F0D9", "E3EDF7", "FFF2CC"]:
        dxf = node(dxfs, "dxf")
        pattern = node(node(dxf, "fill"), "patternFill", {"patternType": "solid"})
        node(pattern, "fgColor", {"rgb": "FF" + color})
        node(pattern, "bgColor", {"indexed": "64"})
    return xml(root)


def worksheet_xml(sheet: dict) -> bytes:
    root = ET.Element(f"{{{NS}}}worksheet")
    rows, width = sheet["rows"], len(sheet["widths"])
    last = col(width)
    node(root, "dimension", {"ref": f"A1:{last}{len(rows)}"})
    view = node(node(root, "sheetViews"), "sheetView", {"workbookViewId": "0", "showGridLines": "0"})
    node(view, "pane", {"ySplit": "4", "topLeftCell": "A5", "activePane": "bottomLeft", "state": "frozen"})
    node(view, "selection", {"pane": "bottomLeft", "activeCell": "A5", "sqref": "A5"})
    node(root, "sheetFormatPr", {"defaultRowHeight": "31"})
    columns = node(root, "cols")
    for number, size in enumerate(sheet["widths"], 1):
        node(columns, "col", {"min": str(number), "max": str(number), "width": str(size), "customWidth": "1"})
    data = node(root, "sheetData")
    for number, values in enumerate(rows, 1):
        row = node(data, "row", {"r": str(number), "ht": str(38 if number in (1, 2) else 31), "customHeight": "1"})
        for column, value in enumerate(values, 1):
            if value is None or value == "":
                continue
            money = (number >= 5 and column in sheet["currency_columns"]) or (column == 2 and number in sheet["currency_rows"])
            style = 1 if number == 1 else 2 if number == 2 else 3 if number == 4 else (6 if number % 2 else 7) if money else (4 if number % 2 else 5)
            cell = node(row, "c", {"r": f"{col(column)}{number}", "s": str(style)})
            if isinstance(value, dict):
                cached = value["cached"]
                if isinstance(cached, str):
                    cell.set("t", "str")
                node(cell, "f", text=value["formula"][1:])
                node(cell, "v", text=cached)
            elif isinstance(value, (int, float)):
                node(cell, "v", text=value)
            else:
                cell.set("t", "inlineStr")
                node(node(cell, "is"), "t", {"{http://www.w3.org/XML/1998/namespace}space": "preserve"}, value)
    if sheet["filter"]:
        node(root, "autoFilter", {"ref": f"A4:{last}{len(rows)}"})
    merges = node(root, "mergeCells", {"count": "2"})
    for number in (1, 2):
        node(merges, "mergeCell", {"ref": f"A{number}:{last}{number}"})
    if sheet["filter"]:
        cf = node(root, "conditionalFormatting", {"sqref": f"I5:I{len(rows)}"})
        for priority, result in enumerate(["BROKER HIGHER", "BROKER LOWER", "EQUAL", "REVIEW"], 1):
            rule = node(cf, "cfRule", {"type": "cellIs", "dxfId": str(priority - 1), "priority": str(priority), "operator": "equal"})
            node(rule, "formula", text=f'"{result}"')
    node(root, "pageMargins", {"left": "0.25", "right": "0.25", "top": "0.5", "bottom": "0.5", "header": "0.2", "footer": "0.2"})
    node(root, "pageSetup", {"orientation": "landscape", "paperSize": "9", "fitToWidth": "1", "fitToHeight": "0"})
    node(node(root, "headerFooter"), "oddFooter", text="&LSIMULATED DATA&RP &P of &N")
    return xml(root)


def make_workbook(model: dict, path: Path) -> None:
    parts: dict[str, bytes] = {}
    types = ET.Element(f"{{{CONTENT}}}Types")
    for extension, mime in [("rels", "application/vnd.openxmlformats-package.relationships+xml"), ("xml", "application/xml")]:
        ET.SubElement(types, f"{{{CONTENT}}}Default", {"Extension": extension, "ContentType": mime})
    overrides = [("/xl/workbook.xml", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"),
                 ("/xl/styles.xml", "application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml")]
    for index in range(1, len(model["sheets"]) + 1):
        overrides.append((f"/xl/worksheets/sheet{index}.xml", "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"))
    for name, mime in overrides:
        ET.SubElement(types, f"{{{CONTENT}}}Override", {"PartName": name, "ContentType": mime})
    parts["[Content_Types].xml"] = xml(types)
    root_rels = ET.Element(f"{{{PKG}}}Relationships")
    ET.SubElement(root_rels, f"{{{PKG}}}Relationship", {"Id": "rId1", "Type": REL + "/officeDocument", "Target": "xl/workbook.xml"})
    parts["_rels/.rels"] = xml(root_rels)
    workbook = ET.Element(f"{{{NS}}}workbook")
    node(node(workbook, "bookViews"), "workbookView", {"activeTab": "0"})
    sheets = node(workbook, "sheets")
    relationships = ET.Element(f"{{{PKG}}}Relationships")
    for index, sheet in enumerate(model["sheets"], 1):
        node(sheets, "sheet", {"name": sheet["name"], "sheetId": str(index), f"{{{REL}}}id": f"rId{index}"})
        ET.SubElement(relationships, f"{{{PKG}}}Relationship", {"Id": f"rId{index}", "Type": REL + "/worksheet", "Target": f"worksheets/sheet{index}.xml"})
        parts[f"xl/worksheets/sheet{index}.xml"] = worksheet_xml(sheet)
    node(workbook, "calcPr", {"calcId": "191029", "fullCalcOnLoad": "1"})
    ET.SubElement(relationships, f"{{{PKG}}}Relationship", {"Id": "rIdStyles", "Type": REL + "/styles", "Target": "styles.xml"})
    parts["xl/workbook.xml"] = xml(workbook)
    parts["xl/_rels/workbook.xml.rels"] = xml(relationships)
    parts["xl/styles.xml"] = styles_xml()
    with ZipFile(path, "w", compression=ZIP_DEFLATED) as archive:
        for name, content in parts.items():
            info = ZipInfo(name, (2026, 10, 17, 0, 0, 0))
            info.compress_type = ZIP_DEFLATED
            archive.writestr(info, content)


def build(output: Path, replace: bool = False) -> list[dict]:
    models = [company_model(index, company) for index, company in enumerate(COMPANIES)]
    targets = [output / model["file"] for model in models] + [output / "manifest.json"]
    if not replace and any(path.exists() for path in targets):
        raise FileExistsError("Reference outputs already exist; use --replace to regenerate the demo references.")
    output.mkdir(parents=True, exist_ok=True)
    for model in models:
        path = output / model["file"]
        make_workbook(model, path)
        model["sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
    manifest = {"version": 1, "notice": NOTICE,
                "layout_reference": "MotorhomeHire vs Supplier Price Checking - revised.xlsx",
                "workbooks": models}
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return models


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--replace", action="store_true", help="Replace only these generated reference files and manifest")
    args = parser.parse_args()
    models = build(args.output, args.replace)
    print(f"Created {len(models)} .xlsx workbooks, 20 sheets, and 240 fictional quote comparisons in {args.output.resolve()}")
