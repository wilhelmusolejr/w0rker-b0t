# Fictional company Excel workbooks

Open any of the ten `.xlsx` files in this folder. Every file has a Summary sheet and a Price Checks sheet with 24 invented quotes. All sheets are marked SIMULATED DATA. The layout is based on the neighboring `MotorhomeHire vs Supplier Price Checking - revised.xlsx`; its actual company prices were not reused.

- [Harbour Trail Rentals](01_Harbour_Trail_Rentals.xlsx)
- [Southern Ridge Campers](02_Southern_Ridge_Campers.xlsx)
- [Blue Wattle Motorhomes](03_Blue_Wattle_Motorhomes.xlsx)
- [Coastal Fern Travel](04_Coastal_Fern_Travel.xlsx)
- [Outback Lantern Hire](05_Outback_Lantern_Hire.xlsx)
- [Silver Creek Campers](06_Silver_Creek_Campers.xlsx)
- [Red Sand Touring](07_Red_Sand_Touring.xlsx)
- [Alpine Cove Rentals](08_Alpine_Cove_Rentals.xlsx)
- [Golden Bay Motorhomes](09_Golden_Bay_Motorhomes.xlsx)
- [Riverstone Road Trips](10_Riverstone_Road_Trips.xlsx)

To enter these figures visibly into fresh Excel workbooks, run `start-reference-excel.cmd` from the `worken` folder. `start-reference-excel.cmd -DryRun` checks the scripts without opening Excel. Playback output goes to a separate `Excel Playback` folder.

See [the Excel demo guide](../../TutorialAutomation/EXCEL_DEMO.md) for prerequisites, settings, resume behavior, and testing. `manifest.json` pairs every workbook with its SHA-256 and the exact cell/formula content used by the player.
