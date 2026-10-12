#!/usr/bin/env python3
"""Checks Kleio's string resources (src/Kleio/Strings/<language>/Resources.resw).

* every .resw parses and has no duplicate keys;
* every language has exactly the keys of en-US and keeps its {0}-style placeholders;
* FlowDirection is LeftToRight or RightToLeft;
* every x:Uid used in XAML has resources and every property resource belongs to an x:Uid;
* every key the code looks up (Strings::Get/Format, Localization::Text, ms-resource:) exists, and
  Core's English fallbacks match en-US;
* the Language setting (Strings.cpp) lists exactly the language folders.

Usage: python tools/check_translations.py   (exit code 1 when something is wrong)
"""

import pathlib
import re
import sys
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parent.parent
APP = ROOT / 'src' / 'Kleio'
STRINGS = APP / 'Strings'
SOURCE = 'en-US'
PLACEHOLDER = re.compile(r'\{\d\}')
# Literals next to string lookups that are not resource keys.
NOT_KEYS = {'RightToLeft'}

errors = []


def error(message):
    errors.append(message)


def load(path):
    try:
        root = ET.parse(path).getroot()
    except ET.ParseError as e:
        error(f'{path.relative_to(ROOT)}: does not parse: {e}')
        return {}
    values = {}
    for data in root.findall('data'):
        name = data.get('name')
        value = data.findtext('value')
        if name in values:
            error(f'{path.relative_to(ROOT)}: duplicate key {name}')
        if value is None:
            error(f'{path.relative_to(ROOT)}: {name} has no value')
            value = ''
        values[name] = value
    return values


def main():
    languages = {p.name: load(p / 'Resources.resw') for p in sorted(STRINGS.iterdir())
                 if (p / 'Resources.resw').is_file()}
    others = [p for p in STRINGS.rglob('*.resw') if p.name != 'Resources.resw']
    for p in others:
        error(f'{p.relative_to(ROOT)}: only Resources.resw is expected')
    source = languages.get(SOURCE)
    if not source:
        print(f'{SOURCE}/Resources.resw is missing')
        return 1

    # ---- languages against en-US
    for language, values in languages.items():
        for key in source.keys() - values.keys():
            error(f'{language}: missing {key}')
        for key in values.keys() - source.keys():
            error(f'{language}: extra key {key}')
        for key in source.keys() & values.keys():
            if sorted(PLACEHOLDER.findall(source[key])) != sorted(PLACEHOLDER.findall(values[key])):
                error(f'{language}: {key} has placeholders {sorted(set(PLACEHOLDER.findall(values[key])))}, '
                      f'en-US has {sorted(set(PLACEHOLDER.findall(source[key])))}')
            if not values[key].strip() and source[key].strip():
                error(f'{language}: {key} is empty')
        if values.get('FlowDirection') not in ('LeftToRight', 'RightToLeft'):
            error(f'{language}: FlowDirection must be LeftToRight or RightToLeft')

    # ---- XAML x:Uid
    uids = {}
    for xaml in APP.rglob('*.xaml'):
        for uid in re.findall(r'x:Uid="([^"]+)"', xaml.read_text(encoding='utf-8')):
            if uid in uids:
                error(f'{xaml.relative_to(ROOT)}: x:Uid {uid} is also used in {uids[uid].relative_to(ROOT)}')
            uids[uid] = xaml
    property_keys = {key for key in source if '.' in key}
    for uid, xaml in uids.items():
        if not any(key.startswith(uid + '.') for key in property_keys):
            error(f'{xaml.relative_to(ROOT)}: x:Uid {uid} has no resources')
    for key in property_keys:
        if key.split('.', 1)[0] not in uids:
            error(f'en-US: {key} belongs to no x:Uid')

    # ---- keys used by the code
    used = set()
    code = list(APP.rglob('*.cpp')) + list(APP.rglob('*.h'))
    literal = re.compile(r'L"([A-Za-z][A-Za-z0-9_]*)"')
    for path in code:
        text = path.read_text(encoding='utf-8')
        for line in text.splitlines():
            lookup = 'Strings::' in line or re.search(r'\bGet\(', line)
            for key in literal.findall(line):
                if not lookup and not (path.name == 'Strings.cpp' and key.startswith('Key_')):
                    continue
                if key in source:
                    used.add(key)
                elif lookup and key not in NOT_KEYS:
                    error(f'{path.relative_to(ROOT)}: no resource {key}')
        for key, fallback in re.findall(r'Localization::Text\(L"([^"]+)",\s*L"((?:[^"\\]|\\.)*)"\)', text):
            used.add(key)
            if key not in source:
                error(f'{path.relative_to(ROOT)}: no resource {key}')
            elif fallback.encode().decode('unicode_escape') != source[key]:
                error(f'{path.relative_to(ROOT)}: English fallback of {key} differs from en-US')
    for path in APP.glob('*.appxmanifest'):
        for key in re.findall(r'ms-resource:([A-Za-z0-9_]+)', path.read_text(encoding='utf-8')):
            used.add(key)
            if key not in source:
                error(f'{path.relative_to(ROOT)}: no resource {key}')
    for key in source:
        if '.' not in key and key not in used:
            error(f'en-US: {key} is not used by the code')

    # ---- Language setting
    listed = set(re.findall(r'\{ L"([A-Za-z-]+)", L"', (APP / 'Strings.cpp').read_text(encoding='utf-8')))
    for language in languages.keys() - listed:
        error(f'Strings.cpp: language {language} is missing from the Language setting')
    for language in listed - languages.keys():
        error(f'Strings.cpp: language {language} has no Strings folder')

    for message in errors:
        print(message)
    print(f'{len(languages)} languages, {len(source)} keys, {len(errors)} problem(s)')
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
