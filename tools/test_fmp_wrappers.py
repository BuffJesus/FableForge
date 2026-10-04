#!/usr/bin/env python3
"""Compare original 12345+zlib FMPs with the audited normalized BIG copies.

The historic corpus is optional and read only. Pass --corpus to its packages.json
and --corpus-root to the directory against which its paths are relative.
"""
import argparse
import hashlib
import json
import subprocess
import tempfile
import zlib
from pathlib import Path
from test_fmp_record_refusal import write_fmp
from test_mod_field_details import write_bank, RECORD, TYPE


def run(tool, *args, ok=True):
    result = subprocess.run([str(tool), 'fmp', *map(str, args)],
                            capture_output=True, timeout=90)
    assert (result.returncode == 0) == ok, (args, result.stderr.decode(errors='replace'))
    return result


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    parser.add_argument('--corpus', type=Path, required=True)
    parser.add_argument('--corpus-root', type=Path, required=True)
    args = parser.parse_args()
    packages = json.loads(args.corpus.read_text(encoding='utf-8'))
    wrapped = [p for p in packages if p.get('normalization')]
    assert len(wrapped) == 73, len(wrapped)
    for package in wrapped:
        original = args.corpus_root/package['local_path']
        normalized = args.corpus_root/package['normalized_path']
        assert hashlib.sha256(original.read_bytes()).hexdigest() == package['sha256']
        a = json.loads(run(args.tool, 'list', original, '--json').stdout)
        b = json.loads(run(args.tool, 'list', normalized, '--json').stdout)
        a.pop('file'); b.pop('file')
        assert a == b, original

    sample = wrapped[0]
    original = args.corpus_root/sample['local_path']
    normalized = args.corpus_root/sample['normalized_path']
    work = Path(tempfile.mkdtemp(prefix='fmp-wrappers-', dir=repo/'build'))
    rewritten, reference = work/'original-rewritten.fmp', work/'normalized-rewritten.fmp'
    run(args.tool, '_rewrite', original, rewritten)
    run(args.tool, '_rewrite', normalized, reference)
    assert rewritten.read_bytes() == reference.read_bytes()

    packed = original.read_bytes()
    malformed = {
        'truncated': packed[:-2],
        'trailing': packed + b'extra',
        'not_big': packed[:4] + zlib.compress(b'not a BIG archive'),
        'oversized': packed[:4] + zlib.compress(b'X' * (256 * 1024 * 1024 + 1)),
    }
    sentinel = work/'prior.fmp'
    sentinel.write_bytes(b'keep prior output')
    for label, body in malformed.items():
        path = work/(label + '.fmp')
        path.write_bytes(body)
        run(args.tool, '_rewrite', path, sentinel, ok=False)
        assert sentinel.read_bytes() == b'keep prior output', label

    base, changed = work/'base', work/'changed'
    write_bank(base, (1, 2, 3, 4))
    write_bank(changed, (10, 2, 3, 4))
    payload = zlib.decompress((changed/'data/CompiledDefs/game.bin').read_bytes()[49:])[2:]
    plain_fmp, wrapped_fmp = work/'fixture.fmp', work/'fixture-wrapped.fmp'
    write_fmp(plain_fmp, [(RECORD, TYPE, payload)])
    wrapped_fmp.write_bytes(b'\x39\x30\x00\x00' + zlib.compress(plain_fmp.read_bytes()))
    for command in ('apply', 'merge'):
        outputs = []
        for label, package in (('plain', plain_fmp), ('wrapped', wrapped_fmp)):
            output = work/(command + '-' + label)
            if command == 'apply':
                run(args.tool, 'apply', base, package, output)
            else:
                result = subprocess.run([str(args.tool), 'mods', 'merge', str(base),
                                         str(output), '--with', str(package)],
                                        capture_output=True, timeout=90)
                assert result.returncode == 0, result.stderr.decode(errors='replace')
            outputs.append((output/'data/CompiledDefs/game.bin').read_bytes())
        assert outputs[0] == outputs[1], command
    print(f'{len(wrapped)} original compressed FMPs match normalized listings; '
          'payload rewrite, apply/merge and malformed-wrapper refusals pass:', work)


if __name__ == '__main__':
    main()
