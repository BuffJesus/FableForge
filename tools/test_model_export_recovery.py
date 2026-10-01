#!/usr/bin/env python3
"""Model exports publish geometry and sidecars together or preserve all previous files."""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from retail_smoke import parse_glb, validate


def main():
    if os.name != 'nt':
        raise SystemExit('this sharing-lock regression requires Windows')
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(os.environ.get('FABLE_ROOT',
        r'C:\Programs\Steam\steamapps\common\Fable The Lost Chapters')))
    parser.add_argument('--exe', type=Path, default=repo / 'build/forge.exe')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='model-export-recovery-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    root = work / 'install'
    for name in ('data/CompiledDefs/game.bin', 'data/CompiledDefs/names.bin',
                 'data/Levels/FinalAlbion.wad', 'data/Levels/FinalAlbion_RT.stb',
                 'data/graphics/pc/textures.big'):
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(args.root / name, target)
    def snapshot(folder):
        result = {}
        for path in folder.rglob('*'):
            if path.is_file():
                with path.open('rb') as stream:
                    result[path.relative_to(folder).as_posix()] = hashlib.file_digest(stream, 'sha256').hexdigest()
        return result
    original = snapshot(root)
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
        ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    for ext in ('glb', 'obj'):
        exports = work / ext
        exports.mkdir()
        output = exports / ('terrain.' + ext)
        metadata = exports / 'terrain.themes.json'
        output.write_bytes(b'previous exported model')
        metadata.write_bytes(b'previous theme metadata')
        sentinel = exports / 'terrain.tmp'
        sentinel.write_bytes(b'unrelated output')
        before = snapshot(exports)
        def run(label, success):
            result = subprocess.run([str(args.exe.resolve()), 'export', 'TeleporterGreatwood',
                '--out', str(output), '--layers', '--install', str(root)],
                cwd=work, capture_output=True, text=True, timeout=180)
            (work / (ext + '_' + label + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
            assert (result.returncode == 0) == success, result.stdout + result.stderr
            if not success:
                assert 'wrote:' not in result.stdout
        handle = kernel.CreateFileW(str(metadata), 0x80000000, 1, None, 3, 0x80, None)
        assert handle != wintypes.HANDLE(-1).value
        try:
            run('locked_metadata', False)
        finally:
            kernel.CloseHandle(handle)
        after = snapshot(exports)
        (work / (ext + '_failure.json')).write_text(json.dumps({'changed_originals':
            [name for name, value in before.items() if after.get(name) != value],
            'extra_files': sorted(set(after) - set(before))}, indent=2), encoding='utf-8')
        assert after == before
        run('retry', True)
        successful = snapshot(exports)
        json.loads(metadata.read_text(encoding='utf-8'))
        if ext == 'glb':
            doc, binary = parse_glb(output)
            validate(doc, binary, True)
        else:
            import trimesh
            scene = trimesh.load(output, force='scene')
            assert scene.geometry
            assert (exports / 'terrain.mtl').is_file() and (exports / 'terrain_albedo.png').is_file()
        run('overwrite', True)
        assert snapshot(exports) == successful
        assert sentinel.read_bytes() == b'unrelated output'
        assert not list(exports.glob('.forge-model-export-*'))
    assert snapshot(root) == original
    print('GLB/OBJ sidecar refusal, retry, structural checks and source preservation: PASS')


if __name__ == '__main__':
    main()
