#!/usr/bin/env python3
"""Argument-only refusal checks; not a successful import or runtime gate."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--forge', type=Path, required=True)
    args = parser.parse_args()
    forge = args.forge.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix='forge-creature-arguments-') as directory:
        root = Path(directory)
        output = root / 'output with spaces'
        base = ['creature-import', str(root / 'model.gltf'), 'ARGUMENT_TEST',
                '--donor-index', '1', '--body', '0', '--alternative', '0',
                '--schema', str(root / 'schema.json'), '--install', str(root / 'source'),
                '--out', str(output)]
        cases = []
        for option in ('--install', '--out', '--schema', '--donor-index', '--body', '--alternative'):
            index = base.index(option)
            cases.append((base[:index] + base[index + 2:], 'required option ' + option))
        for value in ('-1', '+1', '0x1', ' 1', '1 ', '184467440737095516160'):
            command = base.copy()
            command[command.index('--donor-index') + 1] = value
            cases.append((command, 'invalid unsigned decimal --donor-index'))
        cases.extend([
            (base + ['--unknown'], 'unknown option --unknown'),
            (base + ['--body', '1'], 'duplicate option --body'),
            (base + ['--json', '--json'], 'duplicate option --json'),
            (base + ['--texture'], 'missing value for --texture'),
            (base + ['--texture', 'image.png', '--texture-id', '7'], 'choose --texture or --texture-id'),
            (base + ['--texture-format', 'dxt1'], '--texture-format requires --texture'),
            (base + ['--texture-id', '4294967296'], 'invalid unsigned decimal --texture-id'),
        ])
        for name in ('lowercase', 'HAS SPACE', 'A' * 80):
            command = base.copy()
            command[2] = name
            cases.append((command, 'NAME must be 1-79'))
        for command, diagnostic in cases:
            result = subprocess.run([str(forge), *command], cwd=root, capture_output=True, text=True)
            assert result.returncode != 0, command
            assert diagnostic in result.stdout + result.stderr, (command, result.stdout, result.stderr)
            assert not output.exists(), 'argument refusal created output'
        help_result = subprocess.run([str(forge), 'creature-import', '--help'],
                                     cwd=root, capture_output=True, text=True)
        assert help_result.returncode == 0, help_result.stderr
        assert 'forge creature-import' in help_result.stdout + help_result.stderr
        assert not output.exists()
        print(f'PASS: {len(cases)} argument refusals and help; no output created')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
