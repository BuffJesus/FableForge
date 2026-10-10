"""Check emitted names against the native length-bounded table load contract."""
import hashlib
import struct
import subprocess
import tempfile
from pathlib import Path

from test_namespacereview_cli import write_bank


def native_table(raw):
    count, length = struct.unpack_from('<II', raw, 8)
    assert length == len(raw) - 16, 'declared table length excludes part of the stored stream'
    table = raw[16:16 + length]
    position = 4
    names = []
    for _ in range(count):
        assert position + 4 < len(table), 'native stream misses CRC or string'
        end = table.index(0, position + 4)
        names.append(table[position:end + 1])
        position = end + 1
    assert position == len(table), 'unowned bytes after name records'
    return names


def main():
    tool = Path(__file__).resolve().parents[1] / 'build/forge-tools.exe'
    with tempfile.TemporaryDirectory(prefix='forge-names-writer-') as directory:
        root = Path(directory)
        base, source, output = [root / n for n in ('baseline', 'source', 'output')]
        write_bank(base, [''])
        write_bank(source, ['', 'LAST_NAME_MUST_REMAIN_COMPLETE'])
        before = {p: hashlib.sha256(p.read_bytes()).digest() for p in root.rglob('*.bin')}
        for original in (base, source):
            result = subprocess.run([str(tool), 'defs', 'merge', str(original), str(output),
                                     'game.bin', str(original)], capture_output=True, text=True, timeout=30)
            assert result.returncode == 0, (result.stdout, result.stderr)
            expected = (original / 'data/CompiledDefs/names.bin').read_bytes()
            actual = (output / 'data/CompiledDefs/names.bin').read_bytes()
            assert native_table(actual) == native_table(expected)
            assert actual == expected, 'no-op save changed names bytes'
        result = subprocess.run([str(tool), 'defs', 'merge', str(base), str(output),
                                 'game.bin', str(source)], capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, (result.stdout, result.stderr)
        assert native_table((output / 'data/CompiledDefs/names.bin').read_bytes()) == native_table(
            (source / 'data/CompiledDefs/names.bin').read_bytes())
        assert all(hashlib.sha256(p.read_bytes()).digest() == digest for p, digest in before.items())
        legacy = root / 'legacy'
        write_bank(legacy, ['', 'LAST_NAME_MUST_REMAIN_COMPLETE'])
        path = legacy / 'data/CompiledDefs/names.bin'
        raw = bytearray(path.read_bytes())
        struct.pack_into('<I', raw, 12, len(raw) - 20)
        path.write_bytes(raw)
        result = subprocess.run([str(tool), 'defs', 'merge', str(legacy), str(output),
                                 'game.bin', str(legacy)], capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, (result.stdout, result.stderr)
        actual = (output / 'data/CompiledDefs/names.bin').read_bytes()
        assert native_table(actual) == native_table((source / 'data/CompiledDefs/names.bin').read_bytes())
        assert actual[:12] == raw[:12] and actual[16:] == raw[16:]
        assert path.read_bytes() == raw
    print('Names writer: 4 native stream-length and preservation checks passed.')


if __name__ == '__main__':
    main()
