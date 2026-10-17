"""Decode NZ editor reference documents for offline analysis; never execute scripts."""
import json
import re
from email import policy
from email.parser import BytesParser
from pathlib import Path
from importlib import import_module
from urllib.parse import urlparse

parser = import_module('analyze-editor-snapshots')
FOLDER = Path(__file__).resolve().parents[1] / 'references' / 'campervanfinder'
SOURCES = [
    ('vehicle-edit profile nz.mhtml', 'vehicle-editor-nz.before.snapshot.html'),
    ('vehicle-edit profile update disabled, update btn pressed.mhtml', 'vehicle-editor-nz.saving.snapshot.html'),
    ('vehicle-edit profile nz updated.mhtml', 'vehicle-editor-nz.updated.snapshot.html'),
]


def main():
    inventory = []
    for filename, destination in SOURCES:
        message = BytesParser(policy=policy.default).parsebytes((FOLDER / filename).read_bytes())
        part = next(part for part in message.walk() if part.get_content_type() == 'text/html')
        url = part.get('Content-Location') or message.get('Snapshot-Content-Location')
        if urlparse(url).netloc != 'www.campervanfinder.co.nz':
            raise ValueError(f'Unexpected NZ reference origin: {filename}')
        html = part.get_payload(decode=True).decode(part.get_content_charset() or 'utf-8')
        html = re.sub(r'\r+\n', '\n', html)
        # Scripts from saved live pages must not run in fixtures.
        html = re.sub(r'<script\b[^>]*>[\s\S]*?</script>', '', html, flags=re.I)
        document = parser.Document(html)
        controls, messages = [], []
        for node in document.root.nodes():
            name = node.attrs.get('name', '')
            if node.attrs.get('id') == 'message':
                messages.append(node.text().strip())
            if node.tag not in ('input', 'select', 'textarea') or not (
                name in ('post_ID', 'post_type', 'post_title', 'content', 'excerpt', 'post_status',
                         '_override_tab_layout', 'tax_input[product_cat][]', 'tax_input[product_tag]', 'publish', 'save')
                or name.startswith('product_tab_')
            ):
                continue
            control = dict(node.attrs, tag=node.tag)
            if node.tag == 'textarea':
                control['value'] = node.text()
            controls.append(control)
        (FOLDER / destination).write_text(html, encoding='utf-8')
        inventory.append(dict(file=filename, decodedFile=destination, savedAt=message.get('Date'),
                              editUrl=url, controls=controls, messages=messages))
    (FOLDER / 'nz-editable-fields.inventory.json').write_text(
        json.dumps(inventory, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps([dict(file=item['file'], editUrl=item['editUrl'], messages=item['messages'],
                          update=[control for control in item['controls'] if control.get('id') == 'publish'])
                      for item in inventory], ensure_ascii=True, indent=2))


if __name__ == '__main__':
    main()
