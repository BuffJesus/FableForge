#!/usr/bin/env python3
"""Audit numeric identity and metadata order for unnamed historic FMP records."""
import argparse
import collections
import hashlib
import json
import subprocess
import tempfile
import zipfile
from pathlib import Path

from test_fmp_links import read_bank


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--coverage', type=Path, required=True)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    args = parser.parse_args()
    archive_hash = digest(args.archive)
    baseline_path = args.install/'data/CompiledDefs/game.bin'
    names_path = args.install/'data/CompiledDefs/names.bin'
    source_hashes = {str(p): digest(p) for p in (baseline_path, names_path)}
    baseline = read_bank(args.install)
    coverage = json.loads(args.coverage.read_text(encoding='utf-8'))
    refused = [row for row in coverage['rows']
               if 'unnamed or duplicate metadata entry' in row['reason']]
    counters = collections.Counter()
    rows = []
    with tempfile.TemporaryDirectory(prefix='named-fmp-anonymous-', dir=repo/'build') as temp:
        work = Path(temp).resolve()
        assert work != (repo/'build').resolve() and work.is_relative_to((repo/'build').resolve())
        package = work/'current.fmp'
        with zipfile.ZipFile(args.archive) as archive:
            catalogue = {row['file']: row for row in json.loads(archive.read('catalogue.json'))}
            for record in refused:
                payload = archive.read(record['file'])
                assert hashlib.sha256(payload).hexdigest() == catalogue[record['file']]['sha256']
                package.write_bytes(payload)
                result = subprocess.run([str(args.tool), 'fmp', 'list', str(package), '--json'],
                                        capture_output=True, text=True, timeout=30)
                assert result.returncode == 0, (record['file'], result.stderr)
                banks = {bank['bank']: bank['entries'] for bank in json.loads(result.stdout)['banks']}
                game = banks.get('GameBINEntries', [])
                metadata = banks.get('GameBINLinkMetaData', [])
                anonymous = [entry for entry in game if not entry['name']]
                checks = []
                for entry in anonymous:
                    index = entry['id']
                    base = baseline[index] if index < len(baseline) else None
                    checks.append({'id': index, 'definition': entry['definition'],
                                   'baseline_name': base['name'] if base else None,
                                   'baseline_definition': base['definition'] if base else None,
                                   'matches_blank_baseline': bool(base and not base['name'] and
                                                                  base['definition'] == entry['definition'])})
                metadata_pairs = []
                for entry in metadata:
                    ordinal = entry['id'] - 1
                    owner = game[ordinal] if 0 <= ordinal < len(game) else None
                    metadata_pairs.append({'metadata_id': entry['id'], 'metadata_name': entry['name'],
                                           'game_id': owner['id'] if owner else None,
                                           'game_name': owner['name'] if owner else None,
                                           'ordered_owner': bool(owner and
                                               (not entry['name'] or entry['name'] == owner['name']))})
                row = {'file': record['file'], 'game_entries': len(game),
                       'anonymous_entries': len(anonymous), 'metadata_entries': len(metadata),
                       'numeric_checks': checks, 'metadata_pairs': metadata_pairs}
                rows.append(row)
                counters['packages'] += 1
                counters['anonymous_entries'] += len(anonymous)
                counters['baseline_match'] += sum(item['matches_blank_baseline'] for item in checks)
                counters['metadata_entries'] += len(metadata)
                counters['ordered_metadata'] += sum(item['ordered_owner'] for item in metadata_pairs)
                if all(item['matches_blank_baseline'] for item in checks) and all(
                        item['ordered_owner'] for item in metadata_pairs):
                    counters['strict_packages'] += 1
                package.unlink()
    assert digest(args.archive) == archive_hash
    assert source_hashes == {str(p): digest(p) for p in (baseline_path, names_path)}
    report = {'archive_sha256': archive_hash, 'baseline_sha256': source_hashes,
              'counts': dict(counters), 'rows': rows, 'gameplay_tested': False}
    output = (repo/'build/named-fmp-anonymous-audit.json').resolve()
    output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('Anonymous FMP audit:', report['counts'], 'report:', output)


if __name__ == '__main__':
    main()
