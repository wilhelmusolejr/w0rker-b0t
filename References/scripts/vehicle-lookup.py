"""Find a saved vehicle offline and return its file, full JSON or selected fields."""

import argparse
import json
from pathlib import Path
import re
import sys
import unicodedata

DEFAULT_CATALOG = Path(__file__).resolve().parents[1] / "catalog" / "current"
COUNTRIES = ("au", "nz", "ca", "us")
VEHICLE_ID = re.compile(r"veh_[0-9]{6,}$")


class LookupError(ValueError):
    pass


def normalize(value):
    """Ignore case, punctuation and spacing, including Hi5 versus Hi 5."""
    return "".join(c for c in unicodedata.normalize("NFKC", value).casefold() if c.isalnum())


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise LookupError(f"Duplicate JSON key: {key}")
        result[key] = value
    return result


def invalid_constant(value):
    raise LookupError(f"Invalid JSON constant: {value}")


def text(value):
    return value if isinstance(value, str) else ""


def is_link(path):
    return path.is_symlink() or (hasattr(path, "is_junction") and path.is_junction())


def site_records(record):
    for site_id, site in sorted(record["sites"].items()):
        if not isinstance(site, dict) or not isinstance(site.get("records"), dict):
            raise LookupError(f"Invalid site records: {site_id}")
        for external_id, external in sorted(site["records"].items()):
            if not isinstance(external, dict):
                raise LookupError(f"Invalid site record: {site_id}:{external_id}")
            observed, mapping = external.get("observed"), external.get("mapping")
            if not isinstance(observed, dict) or not isinstance(mapping, dict):
                raise LookupError(f"Missing observation/mapping: {site_id}:{external_id}")
            snapshot = observed.get("snapshot")
            if not isinstance(snapshot, dict):
                raise LookupError(f"Missing snapshot: {site_id}:{external_id}")
            data, fields = snapshot.get("data", {}), snapshot.get("fields", {})
            vehicle = data.get("vehicle", {}) if isinstance(data, dict) else {}
            name = text(vehicle.get("name")) if isinstance(vehicle, dict) else ""
            if not name and isinstance(fields, dict):
                name = text(fields.get("name")) or text(fields.get("vehicle_name"))
            yield {
                "siteId": site_id,
                "externalId": external_id,
                "name": name or None,
                "mappingStatus": mapping.get("status"),
                "capturedAt": observed.get("capturedAt"),
                "captureStatus": observed.get("captureStatus"),
            }


def load_catalog(directory):
    directory = Path(directory).absolute()
    if is_link(directory):
        raise LookupError("A catalog directory must not be a symbolic link/junction")
    if not directory.exists():
        return []
    if not directory.is_dir():
        raise LookupError("The catalog path must be a directory")
    entries, seen = [], set()
    for country in COUNTRIES:
        folder = directory / country
        if is_link(folder):
            raise LookupError(f"Symbolic link/junction in catalog: {folder}")
        if not folder.exists():
            continue
        if not folder.is_dir():
            raise LookupError(f"Country catalog must be a directory: {folder}")
        for file in sorted(folder.glob("*.json")):
            if is_link(file):
                raise LookupError(f"Symbolic link/junction in catalog: {file}")
            try:
                record = json.loads(file.read_text(encoding="utf-8-sig"), object_pairs_hook=unique_object, parse_constant=invalid_constant)
                if not isinstance(record, dict) or record.get("schemaVersion") != "1.0" or record.get("recordType") != "vehicleCatalogRecord":
                    raise LookupError("Expected a vehicle catalog record schema 1.0")
                vehicle_id = record.get("vehicleId")
                if not isinstance(vehicle_id, str) or not VEHICLE_ID.fullmatch(vehicle_id):
                    raise LookupError("Invalid permanent vehicle ID")
                if vehicle_id in seen:
                    raise LookupError(f"Duplicate permanent vehicle ID: {vehicle_id}")
                identity = record.get("identity")
                if not isinstance(identity, dict) or identity.get("country") != country:
                    raise LookupError("Country directory disagrees with vehicle identity")
                for key in ("brand", "model"):
                    if not normalize(text(identity.get(key))):
                        raise LookupError(f"Missing vehicle {key}")
                if not file.name.startswith(f"{vehicle_id} - {country} - "):
                    raise LookupError("Filename disagrees with vehicle ID/country")
                revision = record.get("catalogRevision")
                if type(revision) is not int or revision < 1 or not isinstance(record.get("sites"), dict):
                    raise LookupError("Missing catalog revision/sites")
                sites = list(site_records(record))
                seen.add(vehicle_id)
                names = [identity["model"], f'{identity["brand"]} {identity["model"]}']
                variant = text(identity.get("variant"))
                if variant:
                    names.extend([f'{identity["model"]} {variant}', f'{identity["brand"]} {identity["model"]} {variant}'])
                names.extend(site["name"] for site in sites if site["name"])
                entries.append({"file": file, "record": record, "names": {normalize(name) for name in names}, "sites": sites})
            except (OSError, ValueError) as error:
                raise LookupError(f"{file}: {error}") from error
    return entries


def summary(entry):
    record = entry["record"]
    return {
        "vehicleId": record["vehicleId"],
        "file": entry["file"].as_posix(),
        "identity": record["identity"],
        "catalogRevision": record["catalogRevision"],
        "sites": entry["sites"],
    }


def field_value(record, pointer):
    if not pointer.startswith("/") or re.search(r"~(?![01])", pointer):
        raise LookupError(f"Use a JSON Pointer beginning with /: {pointer}")
    value = record
    for token in pointer[1:].split("/"):
        key = token.replace("~1", "/").replace("~0", "~")
        if isinstance(value, dict) and key in value:
            value = value[key]
        elif isinstance(value, list) and re.fullmatch(r"0|[1-9]\d*", key) and int(key) < len(value):
            value = value[int(key)]
        else:
            raise LookupError(f"Saved field does not exist: {pointer}")
    return value


def lookup(directory, *, name=None, vehicle_id=None, country=None, brand=None, exact=False, full=False, fields=()):
    if bool(name) == bool(vehicle_id):
        raise LookupError("Supply either a vehicle name or a permanent vehicle ID")
    if country is not None and country not in COUNTRIES:
        raise LookupError("Country must be au, nz, ca or us")
    if name is not None and not normalize(name):
        raise LookupError("Vehicle name must contain letters or numbers")
    if vehicle_id is not None and not VEHICLE_ID.fullmatch(vehicle_id):
        raise LookupError("Use a permanent vehicle ID such as veh_000021")
    if brand is not None and not normalize(brand):
        raise LookupError("Brand must contain letters or numbers")
    if full and fields:
        raise LookupError("Choose either --full or --field")
    query = {key: value for key, value in {"name": name, "vehicleId": vehicle_id, "country": country, "brand": brand}.items() if value is not None}
    matches = []
    needle = normalize(name) if name is not None else None
    for entry in load_catalog(directory):
        record, identity = entry["record"], entry["record"]["identity"]
        if country is not None and identity["country"] != country:
            continue
        if brand is not None and normalize(brand) not in {normalize(identity["brand"]), normalize(text(identity.get("supplierKey")))}:
            continue
        if vehicle_id is not None:
            matched = record["vehicleId"] == vehicle_id
        else:
            matched = needle in entry["names"] if exact else any(needle in candidate for candidate in entry["names"])
        if matched:
            matches.append(entry)
    result = {"offline": True, "query": query}
    if not matches:
        return {**result, "status": "missing", "nextAction": "checkNameCountryOrCapture", "matches": []}
    if len(matches) != 1:
        return {**result, "status": "ambiguous", "nextAction": "refineNameCountryBrandOrId", "matches": [summary(entry) for entry in matches]}
    entry = matches[0]
    result.update(status="found", nextAction="useStoredVehicle", **summary(entry))
    if full:
        result["data"] = entry["record"]
    elif fields:
        result["fields"] = {pointer: field_value(entry["record"], pointer) for pointer in fields}
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", type=Path, default=DEFAULT_CATALOG, help="Current catalog directory (default: project catalog/current)")
    query = parser.add_mutually_exclusive_group(required=True)
    query.add_argument("--name", help="Canonical model or a saved website vehicle name; partial names are supported")
    query.add_argument("--id", dest="vehicle_id", help="Exact permanent veh_ ID")
    parser.add_argument("--country", choices=COUNTRIES)
    parser.add_argument("--brand", help="Exact catalog brand or supplier key")
    parser.add_argument("--exact", action="store_true", help="Match complete names only, still ignoring case, punctuation and spacing")
    output = parser.add_mutually_exclusive_group()
    output.add_argument("--full", action="store_true", help="Return the complete saved record only for one unique match")
    output.add_argument("--field", action="append", default=[], help="Return a saved JSON Pointer; repeat for multiple fields")
    args = parser.parse_args(argv)
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    try:
        result = lookup(args.catalog, name=args.name, vehicle_id=args.vehicle_id, country=args.country, brand=args.brand, exact=args.exact, full=args.full, fields=args.field)
    except (LookupError, OSError) as error:
        print(json.dumps({"offline": True, "status": "error", "reason": str(error)}, ensure_ascii=False, indent=2))
        return 1
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
