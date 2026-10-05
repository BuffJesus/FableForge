#!/usr/bin/env python3
"""Index named archive entries and candidate providers for retained missing-target errors.

Candidates are evidence for review, not declared dependencies or compatibility.
No package payload is executed or installed.
"""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import re
import struct
import tempfile
import zipfile
import zlib


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def named_entries(data):
    """Read only bounded BIG bank/entry tables; never deserialize metadata."""
    def word(at):
        if at < 0 or at + 4 > len(data):
            raise ValueError('truncated table word')
        return struct.unpack_from('<I', data, at)[0]

    def advance(at, size):
        if at < 0 or size < 0 or at + size > len(data):
            raise ValueError('table span outside package')
        return at + size

    directory = word(8)
    banks = word(directory); cursor = directory + 4
    if banks > 4096:
        raise ValueError('too many banks')
    found = []
    for _ in range(banks):
        end = cursor
        while end < len(data) and data[end] not in (0, 255):
            end += 1
        if end == len(data):
            raise ValueError('unterminated bank name')
        bank = data[cursor:end].decode('ascii')
        cursor = end + 1
        advance(cursor, 20)
        count, start = word(cursor + 4), word(cursor + 8)
        cursor += 20
        if count > len(data) // 28:
            raise ValueError('implausible entry count')
        row = advance(start + 4, word(start) * 8)
        for _ in range(count):
            advance(row, 28)
            name_bytes = word(row + 24)
            row += 28
            name_end = advance(row, name_bytes)
            name = data[row:name_end].decode('ascii')
            row = advance(name_end, 4)
            sources = word(row); row += 4
            if sources > len(data) // 4:
                raise ValueError('implausible source count')
            for _ in range(sources):
                row = advance(row + 4, word(row))
            row = advance(row + 4, word(row))
            if name:
                found.append((bank, name))
    return found


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', required=True, type=Path)
    parser.add_argument('--coverage', required=True, type=Path)
    args = parser.parse_args()
    before = {str(p): digest(p) for p in (args.archive, args.coverage)}
    coverage = json.loads(args.coverage.read_text())
    assert coverage['complete'], 'use a completed coverage report'
    assert coverage['archive_sha256'] == before[str(args.archive)]
    work = Path(tempfile.mkdtemp(prefix='named-fmp-providers-', dir=repo/'build'))
    print('Evidence retained at', work, flush=True)
    providers = collections.defaultdict(list)
    coverage_status = {r['file']: r['status'] for r in coverage['rows']}
    packages, refused = [], []
    with zipfile.ZipFile(args.archive) as archive:
        catalogue = json.loads(archive.read('catalogue.json'))
        for index, row in enumerate(catalogue, 1):
            payload = archive.read(row['file'])
            assert hashlib.sha256(payload).hexdigest() == row['sha256']
            if row['format'] == 'unsupported-layout':
                refused.append({'file': row['file'], 'reason': 'unsupported original layout'})
                continue
            try:
                if row['format'] == 'wrapped-zlib':
                    assert payload[:4] == b'90\0\0'
                    decoder = zlib.decompressobj()
                    payload = decoder.decompress(payload[4:], 256 * 1024 * 1024 + 1)
                    if len(payload) > 256 * 1024 * 1024 or not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
                        raise ValueError('unsupported compressed wrapper')
                assert row['format'] in ('BIG', 'wrapped-zlib')
                entries = named_entries(payload)
                packages.append({'file': row['file'], 'sha256': row['sha256'],
                                 'banks': row['banks'], 'named_entries': len(entries),
                                 'coverage_status': coverage_status.get(row['file'], 'outside_report_scope')})
                for bank, name in set(entries):
                    if bank in ('GameBINEntries', 'Objects', 'Settings', 'maintextures', 'graphics', 'Text', 'text'):
                        providers[name].append({'file': row['file'], 'bank': bank, 'sha256': row['sha256']})
            except (ValueError, struct.error, UnicodeError) as error:
                refused.append({'file': row['file'], 'reason': str(error)})
            if index % 100 == 0:
                print(index, '/', len(catalogue), flush=True)
    missing = []
    missing_textures = []
    for row in coverage['rows']:
        texture = re.fullmatch(r'error: FMP graphics: (.+?) needs missing texture (.+)', row['reason'])
        if texture:
            mesh, target = texture.groups()
            candidates = [p for p in providers.get(target, [])
                          if p['file'] != row['file'] and p['bank'] == 'maintextures']
            missing_textures.append({'consumer': row['file'], 'mesh': mesh, 'target': target,
                'required_bank': 'maintextures', 'candidates': candidates,
                'compatibility_verified': False})
        match = re.search(r': missing target (.+?) for (.+)$', row['reason'])
        if not match:
            continue
        target, field = match.groups()
        candidates = [p for p in providers.get(target, []) if p['file'] != row['file']]
        missing.append({'consumer': row['file'], 'target': target, 'field': field,
                        'candidates': candidates, 'compatibility_verified': False})
    assert before == {str(p): digest(p) for p in (args.archive, args.coverage)}
    report = {'archive_sha256': before[str(args.archive)], 'coverage_sha256': before[str(args.coverage)],
              'indexed_packages': len(packages), 'packages': packages, 'refused_packages': refused,
              'providers': dict(sorted(providers.items())), 'missing_targets': missing,
              'missing_texture_targets': missing_textures,
              'texture_candidate_matches': sum(bool(r['candidates']) for r in missing_textures),
              'candidate_matches': sum(bool(r['candidates']) for r in missing),
              'dependencies_declared': False, 'gameplay_tested': False}
    (work/'result.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('Named provider index:', len(packages), 'packages;', report['candidate_matches'],
          '/', len(missing), 'missing-target rows have candidates; no compatibility claim')
    print('Mesh texture candidates:', report['texture_candidate_matches'], '/', len(missing_textures))


if __name__ == '__main__':
    main()
