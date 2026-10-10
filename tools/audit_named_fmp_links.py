#!/usr/bin/env python3
"""Inventory single and multi-target named links in the historic FMP archive."""
import argparse
import collections
import hashlib
import json
import subprocess
import tempfile
import zipfile
import zlib
from pathlib import Path


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    args = parser.parse_args()
    build = (repo/'build').resolve()
    archive_hash = digest(args.archive)
    rows = []
    group_sizes = collections.Counter()
    multi_fields = collections.Counter()
    target_prefixes = collections.Counter()
    with tempfile.TemporaryDirectory(prefix='named-fmp-links-', dir=build) as temporary:
        work = Path(temporary).resolve()
        assert work != build and work.is_relative_to(build)
        with zipfile.ZipFile(args.archive) as archive:
            catalogue = json.loads(archive.read('catalogue.json'))
            candidates = [r for r in catalogue if any(x in r['banks'] for x in
                           ('GameBINLinkMetaData', 'ObjectLinks'))]
            package = work/'current.fmp'
            for record in candidates:
                payload = archive.read(record['file'])
                assert hashlib.sha256(payload).hexdigest() == record['sha256']
                if record['format'] == 'wrapped-zlib':
                    assert payload[:4] == b'90\0\0'
                    decoder = zlib.decompressobj()
                    payload = decoder.decompress(payload[4:], 256 * 1024 * 1024 + 1)
                    assert len(payload) <= 256 * 1024 * 1024 and decoder.eof
                    assert not decoder.unused_data and not decoder.unconsumed_tail
                package.write_bytes(payload)
                result = subprocess.run([str(args.tool), 'fmp', 'links', str(package),
                                         '--groups', '--json'], capture_output=True, text=True,
                                        timeout=30, cwd=repo)
                row = {'file': record['file'], 'sha256': record['sha256'],
                       'banks': record['banks'], 'error': result.stderr.strip()[-300:] if result.returncode else ''}
                if not result.returncode:
                    groups = [group for entry in json.loads(result.stdout)['records']
                              for group in entry['groups']]
                    row['groups'] = len(groups)
                    row['multi'] = sum(len(group['targets']) > 1 for group in groups)
                    row['empty'] = sum(not group['targets'] for group in groups)
                    for group in groups:
                        n = len(group['targets'])
                        group_sizes[n] += 1
                        if n > 1:
                            path = group['path'].split('.')
                            multi_fields[path[1] if len(path) > 1 else group['path']] += 1
                        for target in group['targets']:
                            target_prefixes[target.split('_')[0]] += 1
                rows.append(row)
                package.unlink()
    assert digest(args.archive) == archive_hash
    report = {'archive_sha256': archive_hash, 'packages': len(rows),
              'decoded': sum(not r['error'] for r in rows),
              'group_sizes': dict(sorted(group_sizes.items())),
              'multi_fields': dict(multi_fields.most_common()),
              'target_prefixes': dict(target_prefixes.most_common()),
              'rows': rows, 'gameplay_tested': False}
    output = build/'named-fmp-link-audit.json'
    output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('Named FMP link audit:', report['decoded'], '/', report['packages'],
          'decoded;', report['group_sizes'], 'report:', output)


if __name__ == '__main__':
    main()
