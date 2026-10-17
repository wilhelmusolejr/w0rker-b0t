"""Inspect saved MHTML editor controls without loading or executing their scripts."""

import json
import re
from email import policy
from email.parser import BytesParser
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parents[1]
VOID = set('area base br col embed hr img input link meta param source track wbr'.split())


class Node:
    def __init__(self, tag='', attrs=(), parent=None):
        self.tag, self.attrs, self.parent = tag, dict(attrs), parent
        self.children = []

    def nodes(self):
        for child in self.children:
            if isinstance(child, Node):
                yield child
                yield from child.nodes()

    def text(self):
        return ''.join(child.text() if isinstance(child, Node) else child for child in self.children)

    def has_class(self, name):
        return name in (self.attrs.get('class') or '').split()


class Document(HTMLParser):
    def __init__(self, html):
        super().__init__(convert_charrefs=True)
        self.root = Node()
        self.stack = [self.root]
        self.feed(html)

    def handle_starttag(self, tag, attrs):
        node = Node(tag, attrs, self.stack[-1])
        self.stack[-1].children.append(node)
        if tag not in VOID:
            self.stack.append(node)

    def handle_startendtag(self, tag, attrs):
        self.handle_starttag(tag, attrs)
        if tag not in VOID:
            self.handle_endtag(tag)

    def handle_endtag(self, tag):
        for index in range(len(self.stack) - 1, 0, -1):
            if self.stack[index].tag == tag:
                del self.stack[index:]
                break

    def handle_data(self, text):
        self.stack[-1].children.append(text)


def text_label(node):
    return re.sub(r'\s+', ' ', node.text()).strip()


def nearest(node, predicate):
    while node:
        if predicate(node):
            return node
        node = node.parent


def read_snapshot(path):
    if path.suffix == '.mhtml':
        message = BytesParser(policy=policy.default).parsebytes(path.read_bytes())
        part = next(part for part in message.walk() if part.get_content_type() == 'text/html')
        html = part.get_payload(decode=True).decode(part.get_content_charset() or 'utf-8', errors='replace')
        url = part.get('Content-Location') or message.get('Snapshot-Content-Location')
        saved_at = message.get('Date')
    else:
        html, url, saved_at = path.read_text(encoding='utf-8-sig'), None, None
    html = re.sub(r'\r+\n', '\n', html)
    root = Document(html).root
    nodes = list(root.nodes())
    ids = {node.attrs['id']: node for node in nodes if node.attrs.get('id')}
    fields = []
    for field in nodes:
        if not field.has_class('acf-field') or not field.attrs.get('data-name'):
            continue
        label = next((node for node in field.nodes() if node.tag == 'label' and nearest(node, lambda n: n.has_class('acf-label'))), None)
        box = nearest(field, lambda n: n.has_class('postbox'))
        heading = next((node for node in box.nodes() if node.tag == 'h2'), None) if box else None
        controls = []
        for node in field.nodes():
            if node.tag not in ('input', 'select', 'textarea'):
                continue
            if nearest(node.parent, lambda n: n.has_class('acf-field')) is not field:
                continue
            control = {key: value for key, value in node.attrs.items() if key in ('id', 'name', 'type', 'value', 'disabled', 'readonly', 'min', 'max', 'step', 'placeholder', 'multiple')}
            control['tag'] = node.tag
            if node.tag == 'textarea':
                control['value'] = node.text()
            if node.attrs.get('type') in ('checkbox', 'radio'):
                control['checked'] = 'checked' in node.attrs
                choice_label = nearest(node, lambda n: n.tag == 'label')
                control['label'] = text_label(choice_label) if choice_label else None
            if node.tag == 'select':
                control['options'] = [dict(value=option.attrs.get('value', option.text()), label=text_label(option), selected='selected' in option.attrs) for option in node.nodes() if option.tag == 'option']
                selected = [option['value'] for option in control['options'] if option['selected']]
                control['value'] = selected if 'multiple' in node.attrs else (selected[0] if selected else (control['options'][0]['value'] if control['options'] else None))
            controls.append(control)
        attachments = [dict(attachmentId=int(node.attrs['data-id']), position=index + 1) for index, node in enumerate(node for node in field.nodes() if node.has_class('acf-gallery-attachment') and (node.attrs.get('data-id') or '').isdigit())]
        fields.append(dict(name=field.attrs['data-name'], key=field.attrs.get('data-key'), type=field.attrs.get('data-type'), label=text_label(label) if label else None, group=text_label(heading) if heading else None, required=field.attrs.get('data-required'), controls=controls, attachments=attachments))
    taxonomies = {}
    for node in nodes:
        match = re.fullmatch(r'tax_input\[([^]]+)\]\[\]', node.attrs.get('name') or '')
        if node.tag != 'input' or node.attrs.get('type') != 'checkbox' or not match:
            continue
        label = nearest(node, lambda n: n.tag == 'label')
        taxonomies.setdefault(match[1], []).append(dict(termId=int(node.attrs['value']), label=text_label(label) if label else None, checked='checked' in node.attrs))
    post_id = ids.get('post_ID', Node()).attrs.get('value') or parse_qs(urlparse(url or '').query).get('post', [None])[0]
    permalink = next((node.attrs.get('href') for node in ids.get('sample-permalink', Node()).nodes() if node.tag == 'a'), None)
    preview = next((node for node in ids.get('postimagediv', Node()).nodes() if node.tag == 'img'), None)
    status = ids.get('post-status-display')
    return dict(file=path.name, savedAt=saved_at, editUrl=url, postId=int(post_id) if post_id else None, title=ids.get('title', Node()).attrs.get('value'), slug=ids.get('post_name', Node()).attrs.get('value') or ids.get('editable-post-name-full', Node()).text(), publicUrl=permalink, locale=next((node.attrs.get('lang') for node in nodes if node.tag == 'html'), None), statusDisplay=text_label(status) if status else None, featuredImageId=ids.get('_thumbnail_id', Node()).attrs.get('value'), featuredImagePreview=preview.attrs if preview else None, fields=fields, taxonomies=taxonomies)


def main():
    folder = ROOT / 'references' / 'motorhomehire'
    snapshots = [read_snapshot(path) for path in sorted(folder.glob('*.mhtml'))]
    output = folder / 'vehicle-editor-fields-comparison.json'
    output.write_text(json.dumps(snapshots, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    for snapshot in snapshots:
        print(f"{snapshot['file']}: post {snapshot['postId']}, {len(snapshot['fields'])} ACF fields, taxonomies {list(snapshot['taxonomies'])}")
    print(f'Inventory saved: {output}')


if __name__ == '__main__':
    main()
