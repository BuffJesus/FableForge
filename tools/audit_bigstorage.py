"""Hash-pinned BIGB envelope and entry-storage inventory through forge-tools.

Records every refusal without changing archives. Supported envelopes do not
certify their payloads; supported payloads do not resolve cross-asset references
or admit sources. The CLI reviews only its explicitly supported bank versions.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, action='append', required=True)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    roots = [p.resolve() for p in args.source]
    tool, output = args.tool.resolve(), args.out.resolve()
    tooling = [tool, Path(__file__).resolve()]
    if output in tooling or any(output.is_relative_to(root) for root in roots):
        parser.error('report must not replace source files or tooling')
    hashes = {str(p): digest(p) for p in tooling}
    report = {'scope': __doc__, 'source_admitted': False, 'sources': []}
    refused = 0
    for root in roots:
        paths = sorted(p for p in root.rglob('*') if p.is_file() and p.suffix.lower() == '.big')
        if not paths:
            raise RuntimeError(f'no BIG files under {root}')
        reviews = []
        for path in paths:
            hashes[str(path)] = digest(path)
            run = subprocess.run([str(tool), 'world', 'review-storage', str(path), '--json'],
                                 capture_output=True, text=True, encoding='utf-8', timeout=180)
            if run.returncode not in (0, 1):
                raise RuntimeError(f'storage command failed: {run.stderr.strip()}')
            result = json.loads(run.stdout)
            if result.get('source_admitted') is not False or result.get('storage_supported') != (run.returncode == 0):
                raise RuntimeError('inconsistent storage report')
            units = result['units']
            envelope = units[0]
            row = {'path': str(path), 'envelope_supported': envelope['supported'],
                   'storage_supported': result['storage_supported'],
                   'accepted_entries': sum(u['supported'] for u in units[1:]),
                   'refusals': [u for u in units if not u['supported']]}
            if envelope['supported']:
                if envelope.get('scope') != 'archive envelope' or len(units)-1 != envelope['metrics']['entries']:
                    raise RuntimeError('incomplete archive entry review')
                row['metrics'] = envelope['metrics']
            reviews.append(row)
            refused += not row['storage_supported']
            print(path, 'envelope', row['envelope_supported'], 'accepted entries', row['accepted_entries'],
                  'refusals', len(row['refusals']), flush=True)
        report['sources'].append({'root': str(root), 'reviews': reviews})
    for path, expected in hashes.items():
        if digest(Path(path)) != expected:
            raise RuntimeError(f'input changed during review: {path}')
    report.update(input_sha256=hashes, refused_archives=refused)
    output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    return int(refused != 0)


if __name__ == '__main__':
    raise SystemExit(main())
