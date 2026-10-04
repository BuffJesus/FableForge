#!/usr/bin/env python3
"""Classify the named historic FMP archive against Forge's current importer.

Reads the archive and installed baseline; every FMP apply runs in a verified
scratch child under build/. This does not stage, deploy, or test game behavior.
"""
import argparse
import collections
import hashlib
import json
import shutil
import subprocess
import tempfile
import zipfile
from pathlib import Path


SUPPORTED_BANKS = {
    'GameBINEntries', 'GameBINLinkMetaData', 'Objects', 'Settings',
    'ObjectLinks', 'Text', 'FinalAlbionWAD', 'maintextures',
    'graphics', 'graphicsLinkMetaData',
}


def category(message):
    if 'FMP links:' in message:
        return 'FMP links'
    if 'FMP records:' in message:
        return 'FMP records'
    if 'FMP banks:' in message:
        return 'FMP banks'
    if 'no schema for def type:' in message:
        return 'schema unavailable'
    if 'FMP' in message:
        return 'other FMP'
    return 'other'


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def checked_remove(path, root):
    resolved = path.resolve()
    assert resolved != root.resolve() and resolved.is_relative_to(root.resolve()), resolved
    if resolved.exists():
        shutil.rmtree(resolved)


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', required=True, type=Path)
    parser.add_argument('--install', required=True, type=Path)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    parser.add_argument('--limit', type=int, default=0, help='First N eligible packages; 0 = all')
    parser.add_argument('--start-index', type=int, default=1,
                        help='One-based first eligible package to audit; records the selected scope')
    parser.add_argument('--timeout', type=float, default=90,
                        help='Seconds allowed per import; timeouts remain inconclusive rows')
    parser.add_argument('--apply-textures', action='store_true',
                        help='Also write the large textures.big scratch result for each texture package')
    parser.add_argument('--apply-graphics', action='store_true',
                        help='Also write the large graphics.big scratch result for each graphics package')
    args = parser.parse_args()
    if args.start_index < 1 or args.timeout <= 0 or args.limit < 0:
        parser.error('start-index and timeout must be positive; limit cannot be negative')
    work = Path(tempfile.mkdtemp(prefix='named-fmp-coverage-', dir=repo/'build')).resolve()
    print('Evidence retained at', work, flush=True)
    archive_hash = digest(args.archive)
    tool_hash = digest(args.tool)
    baseline = args.install/'data/CompiledDefs'
    baseline_hashes = {name: digest(baseline/name) for name in ('game.bin', 'names.bin')}
    results = []
    with zipfile.ZipFile(args.archive) as archive:
        catalogue = json.loads(archive.read('catalogue.json'))
        eligible = [r for r in catalogue if r['banks'] and set(r['banks']) <= SUPPORTED_BANKS]
        eligible_archive_total = len(eligible)
        eligible = eligible[args.start_index - 1:]
        if args.limit:
            eligible = eligible[:args.limit]
        def checkpoint(complete=False):
            # Retain completed rows even if a later package or the process fails.
            # A partial checkpoint is never presented as a completed corpus gate.
            report = {
                'archive': str(args.archive.resolve()), 'archive_sha256': archive_hash,
                'baseline': str(args.install.resolve()), 'tool': str(args.tool.resolve()),
                'tool_sha256': tool_hash, 'baseline_sha256': baseline_hashes,
                'eligible_total': len(eligible), 'all_catalogue_total': len(catalogue),
                'eligible_archive_total': eligible_archive_total, 'start_index': args.start_index,
                'timeout_seconds': args.timeout,
                'processed_total': len(results), 'complete': complete,
                'apply_textures': args.apply_textures, 'apply_graphics': args.apply_graphics,
                'status_counts': dict(collections.Counter(r['category'] for r in results)),
                'rows': results, 'gameplay_tested': False,
            }
            temporary = work/'result.json.tmp'
            temporary.write_text(json.dumps(report, indent=2), encoding='utf-8')
            temporary.replace(work/'result.json')
        checkpoint()
        for index, record in enumerate(eligible):
            payload = archive.read(record['file'])
            assert hashlib.sha256(payload).hexdigest() == record['sha256'], record['file']
            deferred = []
            if 'maintextures' in record['banks'] and not args.apply_textures:
                deferred.append('texture')
            if 'graphics' in record['banks'] and not args.apply_graphics:
                deferred.append('graphics')
            if deferred:
                results.append({'file': record['file'], 'sha256': record['sha256'],
                                'banks': record['banks'], 'status': 'not_applied_large_bank',
                                'category': 'large ' + ' and '.join(deferred) + ' bank',
                                'reason': 'Use the matching --apply-textures/--apply-graphics flags '
                                          'for a full scratch apply; the named asset audits inspect '
                                          'entry metadata without writing the large banks.'})
                checkpoint()
                continue
            source = work/'current.fmp'
            source.write_bytes(payload)
            output = work/'current-output'
            command = [str(args.tool.resolve()), 'fmp', 'apply', str(args.install.resolve()),
                       str(source), str(output)]
            try:
                result = subprocess.run(command, capture_output=True, timeout=args.timeout, cwd=repo)
                stderr = result.stderr.decode('utf-8', errors='replace').strip()
                row = {'file': record['file'], 'sha256': record['sha256'],
                       'banks': record['banks'], 'status': 'importable_offline' if result.returncode == 0 else 'refused',
                       'category': 'accepted' if result.returncode == 0 else category(stderr),
                       'reason': stderr[-1500:] if result.returncode else ''}
            except subprocess.TimeoutExpired:
                row = {'file': record['file'], 'sha256': record['sha256'],
                       'banks': record['banks'], 'status': 'not_applied_timeout',
                       'category': 'apply timeout',
                       'reason': f'Import exceeded {args.timeout:g} seconds; compatibility is unverified.'}
            results.append(row)
            checkpoint()
            checked_remove(output, work)
            source.unlink()
            if (index + 1) % 50 == 0 or index + 1 == len(eligible):
                print(index + 1, '/', len(eligible),
                      dict(collections.Counter(r['category'] for r in results)), flush=True)
    assert digest(args.archive) == archive_hash
    assert digest(args.tool) == tool_hash, 'Importer binary changed during the corpus audit'
    assert {name: digest(baseline/name) for name in baseline_hashes} == baseline_hashes
    checkpoint(complete=True)
    print('Result:', work/'result.json', flush=True)


if __name__ == '__main__':
    main()
