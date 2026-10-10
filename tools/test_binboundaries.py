"""Malformed BIN framing must refuse without hiding records or changing inputs."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

from test_namespacereview_cli import write_bank


def main():
    tool = Path(__file__).resolve().parents[1] / 'build/forge-tools.exe'
    with tempfile.TemporaryDirectory(prefix='forge-bin-boundaries-') as directory:
        root = Path(directory)
        base = root / 'baseline'
        write_bank(base, ['', 'A'])
        original = (base / 'data/CompiledDefs/game.bin').read_bytes()
        count_pos, directory_pos = 37, 41

        def word(offset, value):
            raw = bytearray(original)
            struct.pack_into('<I', raw, offset, value)
            return bytes(raw)

        def chunk(decoded, compressed=None):
            compressed = zlib.compress(decoded) if compressed is None else compressed
            return (original[:count_pos] + struct.pack('<5I', 2, 0, 0, 2, len(compressed)) +
                    struct.pack('<I', len(compressed)) + compressed)

        cases = [
            ('valid', original, True),
            ('empty-payloads', chunk(struct.pack('<HH', 4, 4)), True),
            ('full-buffer', chunk(struct.pack('<HH', 4, 4) + bytes(65532)), True),
            ('missing-sentinel', word(count_pos, 0), False),
            ('truncated-main', word(9, 0xffffffff), False),
            ('truncated-directory', original[:directory_pos + 7], False),
            ('first-entry', word(directory_pos, 1), False),
            ('first-compressed', word(directory_pos + 4, 1), False),
            ('wrong-end-entry', word(directory_pos + 16, 1), False),
            ('repeated-entry', word(directory_pos + 8, 0), False),
            ('entry-outside', word(directory_pos + 8, 3), False),
            ('empty-compressed', word(directory_pos + 12, 0), False),
            ('compressed-outside', word(directory_pos + 12, 0xffffffff), False),
            ('wrong-total', word(directory_pos + 24, 0), False),
            ('trailing-file', original + b'x', False),
            ('offset-in-table', chunk(struct.pack('<HH', 2, 4) + b'abc'), False),
            ('offset-past-end', chunk(struct.pack('<HH', 4, 99) + b'abc'), False),
            ('offset-decreasing', chunk(struct.pack('<HH', 4, 3) + b'abc'), False),
            ('leading-gap', chunk(struct.pack('<HH', 5, 6) + b'abc'), False),
            ('short-offset-table', chunk(struct.pack('<H', 4)), False),
            ('truncated-zlib', chunk(b'', zlib.compress(b'\x04\0\x04\0')[:-1]), False),
            ('trailing-zlib', chunk(b'', zlib.compress(b'\x04\0\x04\0') + b'x'), False),
            ('oversize-inflate', chunk(struct.pack('<HH', 4, 4) + bytes(65533)), False),
        ]
        for label, raw, accepted in cases:
            source = root / label
            write_bank(source, ['', 'A'])
            (source / 'data/CompiledDefs/game.bin').write_bytes(raw)
            before = {p: hashlib.sha256(p.read_bytes()).digest() for p in source.rglob('*.bin')}
            result = subprocess.run([str(tool), 'defs', 'review-namespace', str(base), str(source), '--json'],
                                    capture_output=True, text=True, encoding='utf-8', timeout=30)
            assert result.returncode == (0 if accepted else 1), (label, result.stdout, result.stderr)
            report = json.loads(result.stdout)
            assert report['analysis_complete'] == accepted, (label, report)
            assert not report['source_admitted']
            if not accepted:
                assert 'bin:' in report['reason'], (label, report)
            assert before == {p: hashlib.sha256(p.read_bytes()).digest() for p in source.rglob('*.bin')}
        empty = root / 'empty'
        write_bank(empty, [])
        result = subprocess.run([str(tool), 'defs', 'review-namespace', str(empty), str(empty), '--json'],
                                capture_output=True, text=True, encoding='utf-8', timeout=30)
        assert result.returncode == 0 and json.loads(result.stdout)['planned_entries'] == 0, result.stdout
    print(f'BIN framing: {len(cases) + 1} checks passed; inputs unchanged.')


if __name__ == '__main__':
    main()
