"""Decode the supplied MHTML references; never execute their scripts."""
import json
import re
import sys
from email import policy
from email.parser import BytesParser
from pathlib import Path
from importlib import import_module

parser = import_module('analyze-editor-snapshots')
FOLDER = Path(__file__).resolve().parents[1] / 'references' / 'campervanfinder'


def main():
    inventory = []
    for path in sorted(FOLDER.glob('*.mhtml')):
        message = BytesParser(policy=policy.default).parsebytes(path.read_bytes())
        part = next(part for part in message.walk() if part.get_content_type() == 'text/html')
        html = part.get_payload(decode=True).decode(part.get_content_charset() or 'utf-8')
        html = re.sub(r'\r+\n', '\n', html)
        document = parser.Document(html)
        controls = []
        for node in document.root.nodes():
            name = node.attrs.get('name', '')
            if node.tag not in ('input', 'select', 'textarea') or not (name in ('post_title', 'content', 'excerpt', '_override_tab_layout', 'tax_input[product_cat][]', 'tax_input[product_tag]') or name.startswith('product_tab_')):
                continue
            control = dict(node.attrs, tag=node.tag)
            if node.tag == 'textarea':
                control['value'] = node.text()
            if node.attrs.get('type') == 'checkbox':
                control['checked'] = 'checked' in node.attrs
            controls.append(control)
        url = part.get('Content-Location') or message.get('Snapshot-Content-Location')
        item = dict(file=path.name, savedAt=message.get('Date'), editUrl=url, controls=controls)
        if path.stem == 'list of products':
            (FOLDER / 'vehicle-list.snapshot.html').write_text(html, encoding='utf-8')
            continue
        inventory.append(item)
        if path.stem.endswith(' - tabs'):
            (FOLDER / 'vehicle-editor.snapshot.html').write_text(html, encoding='utf-8')
            (FOLDER / 'reference-capture.json').write_text(json.dumps({k: v for k, v in item.items() if k != 'controls'}, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    (FOLDER / 'editable-fields.inventory.json').write_text(json.dumps(inventory, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f'Decoded {len(inventory)} Campervan Finder references.')


if __name__ == '__main__':
    main()
