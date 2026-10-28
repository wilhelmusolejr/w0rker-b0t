# Excel company reference demo

The Excel counterpart to `start-reference-coding.cmd` includes ten real `.xlsx` reference files and a launcher that enters their contents through the existing native tutorial player. The layout follows `References/MotorhomeHire vs Supplier Price Checking - revised.xlsx`. Every company, quote, and note in the new files is fictional; every sheet is marked **SIMULATED DATA**. The original reference workbook is unchanged.

## Files and contents

The finished examples are in [References/excel-simulation](../References/excel-simulation/). Each workbook contains:

- **Summary:** quote counts, equal/lower/higher/review counts, average gap, largest absolute gap, and available quote totals, calculated from the comparison sheet.
- **Price Checks:** 24 quotes across four routes, two vehicle types, and three cover options. Columns cover company, route, search settings, dates, three prices, formula-based gap and result, notes, and follow-up.

There are **240 quotes across 20 sheets**. Each company has two unavailable broker quotes to exercise REVIEW, as well as equal, lower, and higher prices. Price totals are labeled separately because available broker totals exclude unavailable quotes while supplier totals cover all quotes. Files have currency formatting, colored results, filters, frozen headings, and cached formula results for readers that do not recalculate immediately.

| Workbook | Fictional company |
|---|---|
| `01_Harbour_Trail_Rentals.xlsx` | Harbour Trail Rentals |
| `02_Southern_Ridge_Campers.xlsx` | Southern Ridge Campers |
| `03_Blue_Wattle_Motorhomes.xlsx` | Blue Wattle Motorhomes |
| `04_Coastal_Fern_Travel.xlsx` | Coastal Fern Travel |
| `05_Outback_Lantern_Hire.xlsx` | Outback Lantern Hire |
| `06_Silver_Creek_Campers.xlsx` | Silver Creek Campers |
| `07_Red_Sand_Touring.xlsx` | Red Sand Touring |
| `08_Alpine_Cove_Rentals.xlsx` | Alpine Cove Rentals |
| `09_Golden_Bay_Motorhomes.xlsx` | Golden Bay Motorhomes |
| `10_Riverstone_Road_Trips.xlsx` | Riverstone Road Trips |

## Run

From the `worken` folder:

```powershell
# Check references, checksums, configuration, and destination ownership.
.\start-reference-excel.cmd -ValidateOnly

# Also generate and validate all 20 worksheet input scripts; no Excel required.
.\start-reference-excel.cmd -DryRun

# Run the visible Excel demo.
.\start-reference-excel.cmd
```

Live playback requires **licensed desktop Microsoft Excel for Windows**, with English UI, dot decimal separator, and comma formula separator, plus the existing `TutorialAutomation/build/tutorial.exe`. The development machine has WPS Spreadsheets registered as `Excel.Application`; the launcher checks the registered executable and reports this before opening an app. Microsoft Excel was not installed. The workbook packages, calculations, native input scripts, and resume comparison helpers were tested without it; live Excel typing, COM integration, and browser return still need an acceptance run on a machine with Excel.

The launcher opens its own visible Excel instance and creates copies in `worken/Excel Playback`. It turns off cell AutoComplete for the session (restoring your setting on exit) and prepares blank sheet formatting through Excel's object model, then the native player enters all labels, numbers, and formulas visibly with paced keystrokes. It reads cells back through Excel after every four entered rows, checks formulas, and saves a checkpoint. On completion it verifies every formula result, adds result colors and a filter, saves, and promotes the `.in-progress.xlsx` file to its final name. Source examples are never played over or copied into the output.

The first input script has a ten-second countdown. Typing uses the reference coding profile: 140-300 ms per character, 900-2,200 ms for newlines, and 6-14 second reading pauses after 160-320 characters. This is a long demonstration. While a native input script is running, **F8 pauses/resumes; F9 or F12 stops**. Keep the desktop unlocked and avoid manual input. Each row is addressed through Excel Go To, so the demo does not depend on cell screen coordinates.

After roughly 300-600 seconds spent in typing chunks, the demo saves a verified checkpoint and opens a Microsoft Excel reference page for 120-180 seconds using the existing browser player, then returns to Excel. The threshold is checked between chunks; long chunks and manual pauses can extend the interval. Browser time is excluded from subsequent work intervals.

## Settings and resume

[scripts/excel-job.json](scripts/excel-job.json) controls paths, countdown, checkpoint size, typing/action timing, and browser visits. Relative paths are resolved from that JSON file. Set `browser.enabled` to `false` for an Excel-only recording. `-Config path/to/another-job.json` selects another job. Dry-run review scripts are written under `.tmp/excel-playback-plan`; dry-run creates no playback workbooks and sends no input.

Run the launcher again to resume. A progress sidecar records which reference owns each output; an exclusive lock prevents two launchers writing the same destination. Existing owned final workbooks are opened read-only and fully verified before being skipped. In-progress workbooks are checked for matching cells, and only rows with missing cells are re-entered. Different numbers, text, formulas, sheet names, extra cells, changed references, or unowned output files stop the job. No conflicting workbook is overwritten.

If interrupted, the launcher closes its own workbook without saving the unfinished chunk, retaining its previous checkpoint. If Excel remains in cell-edit mode and refuses automation, press Esc and close that demo window **without saving**, then rerun. Only the current unsaved chunk needs replay. No automatic restart is enabled.

To begin a fresh recording, choose another empty destination in the job configuration. To intentionally regenerate the source examples and their paired manifest:

```powershell
python TutorialAutomation/scripts/New-ExcelSimulation.py --replace
```

The generator uses only Python's standard library. Its default refuses to replace existing references. Regenerating references does not reset playback ownership, so use a new playback destination if their contents change. Formula serialization and Excel save behavior follow Microsoft's [SpreadsheetML cell documentation](https://learn.microsoft.com/en-us/dotnet/api/documentformat.openxml.spreadsheet.cell) and [Workbook.SaveAs documentation](https://learn.microsoft.com/en-us/office/vba/api/excel.workbook.saveas).

## Verification

```powershell
python -m unittest discover -s TutorialAutomation/tests -p test_excel_simulation.py -v
powershell -NoProfile -ExecutionPolicy Bypass -File TutorialAutomation/tests/excel_playback_tests.ps1
cmake -S TutorialAutomation -B TutorialAutomation/build
ctest --test-dir TutorialAutomation/build --output-on-failure
```

The Python checks independently recompute comparison outcomes and summary arithmetic, inspect every workbook cell and formula cache, verify package XML/checksums, rebuild deterministic references, and reject conflicting/unowned outputs. PowerShell helper checks use fake worksheet matrices to cover checkpoint resume, text/numeric/formula conflicts, extra cells, calculated results, script quoting/addressing, and player log handling. The dry-run check validates all generated scripts with the native parser. None of these checks sends input to Excel.
