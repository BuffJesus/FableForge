#!/usr/bin/env python3
"""Exercise Restore comparison, cleanup and Windows file-lock errors on synthetic files."""
import ctypes
import argparse
import json
from ctypes import wintypes
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    if os.name != 'nt':
        raise SystemExit('this file-sharing regression requires Windows')
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=repo / 'build/forge.exe')
    exe = parser.parse_args().exe.resolve()
    work = Path(tempfile.mkdtemp(prefix='restore-failures-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
        wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.CloseHandle.restype = wintypes.BOOL
    def lock(path, sharing):
        handle = kernel.CreateFileW(str(path), 0x80000000, sharing, None, 3, 0x80, None)
        assert handle != ctypes.c_void_p(-1).value, ctypes.get_last_error()
        return handle
    def fixture(name, current, original):
        root = work / name
        bank = root / 'data/CompiledDefs/game.bin'
        bank.parent.mkdir(parents=True)
        bank.write_bytes(current)
        backup = Path(str(bank) + '.forge-orig')
        backup.write_bytes(original)
        return root, bank, backup
    def run(root, name, command='restore', forget=False):
        args = [str(exe), command, '--install', str(root)]
        if forget:
            args.append('--forget')
        result = subprocess.run(args, cwd=repo, capture_output=True, text=True, timeout=30)
        (work / (name + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        return result
    failures = []
    root, bank, backup = fixture('unreadable', b'equal bytes', b'equal bytes')
    handle = lock(bank, 0)
    try:
        result = run(root, 'unreadable', command='backups')
        forget_unreadable = run(root, 'unreadable_forget', forget=True)
    finally:
        kernel.CloseHandle(handle)
    if '1 differ' not in result.stdout:
        failures.append('unreadable equal-size target was reported identical')
    assert forget_unreadable.returncode != 0, 'forget succeeded without reading its target'
    assert bank.read_bytes() == backup.read_bytes() == b'equal bytes'

    root, bank, backup = fixture('forget', b'equal bytes', b'equal bytes')
    staged = Path(str(bank) + '.forgebak')
    staged.write_bytes(b'equal bytes')
    assert run(root, 'keep').returncode == 0 and backup.exists(), 'ordinary restore removed its baseline'
    result = run(root, 'forget', forget=True)
    if result.returncode or backup.exists():
        failures.append('--forget retained an unchanged original backup')
    assert bank.read_bytes() == b'equal bytes' and staged.read_bytes() == b'equal bytes', 'forget changed a target or unowned staged backup'

    root, bank, backup = fixture('locked_target', b'changed', b'original')
    handle = lock(bank, 1)  # allow reads, deny replacement
    try:
        result = run(root, 'locked_target', forget=True)
    finally:
        kernel.CloseHandle(handle)
    if result.returncode == 0 or 'cannot replace' not in result.stdout + result.stderr or ctypes.FormatError(0).strip().rstrip('.').casefold() in (result.stdout + result.stderr).casefold():
        failures.append('locked target did not report its replacement failure accurately')
    assert bank.read_bytes() == b'changed' and backup.read_bytes() == b'original'
    assert not Path(str(bank) + '.forge-restore').exists(), 'failed replacement leaked a temp file'
    assert run(root, 'target_retry', forget=True).returncode == 0
    assert bank.read_bytes() == b'original' and not backup.exists(), 'retry failed to restore/forget'

    root, bank, backup = fixture('locked_backup', b'changed', b'original')
    handle = lock(backup, 1)
    try:
        result = run(root, 'locked_backup', forget=True)
    finally:
        kernel.CloseHandle(handle)
    if result.returncode == 0:
        failures.append('--forget reported success when backup removal failed')
    assert bank.read_bytes() == backup.read_bytes() == b'original', 'cleanup failure lost original data'
    result = run(root, 'backup_retry', forget=True)
    if result.returncode or backup.exists():
        failures.append('retry did not remove the now-unchanged backup')
    root, bank, backup = fixture('locked_marker', b'original', b'original')
    created = root / 'data/Levels/FinalAlbion/Added.lev'
    created.parent.mkdir(parents=True)
    created.write_bytes(b'created')
    marker = Path(str(created) + '.forge-created')
    marker.write_text('created by FableForge\n')
    handle = lock(marker, 1)
    try:
        result = run(root, 'locked_marker')
    finally:
        kernel.CloseHandle(handle)
    assert result.returncode != 0 and 'cannot remove creation marker' in result.stdout + result.stderr
    assert not created.exists() and marker.exists(), 'marker cleanup failure lost recovery state'
    assert run(root, 'marker_retry').returncode == 0
    assert not created.exists() and not marker.exists(), 'retry retained the orphaned creation marker'
    assert bank.read_bytes() == backup.read_bytes() == b'original'
    root, bank, backup = fixture('failed_stage', b'edited after stage', b'staged content')
    manifest = root / 'forge_stage_manifest.json'
    manifest.write_text(json.dumps({'files': [{'path': 'data/CompiledDefs/game.bin', 'had_original': True}]}))
    # Missing staged original must stop before an ordinary backup can be applied.
    result = run(root, 'failed_stage', forget=True)
    assert result.returncode != 0 and manifest.exists()
    assert bank.read_bytes() == b'edited after stage', 'failed stage continued into ordinary restore'
    assert backup.read_bytes() == b'staged content', 'failed stage consumed an ordinary backup'
    staged_backup = Path(str(bank) + '.forgebak')
    staged_backup.write_bytes(b'retail content')
    # Ensure the ordinary baseline is recognizably newer than the staged one.
    os.utime(staged_backup, (1_700_000_000, 1_700_000_000))
    assert run(root, 'failed_stage_retry', forget=True).returncode == 0
    assert bank.read_bytes() == b'retail content'
    assert not backup.exists() and not staged_backup.exists() and not manifest.exists()
    root, bank, backup = fixture('locked_rebase', b'edited after stage', b'staged content')
    staged_backup = Path(str(bank) + '.forgebak')
    staged_backup.write_bytes(b'retail content')
    os.utime(staged_backup, (1_700_000_000, 1_700_000_000))
    manifest = root / 'forge_stage_manifest.json'
    manifest.write_text(json.dumps({'files': [{'path': 'data/CompiledDefs/game.bin', 'had_original': True}]}))
    handle = lock(backup, 1)
    try:
        result = run(root, 'locked_rebase', forget=True)
    finally:
        kernel.CloseHandle(handle)
    assert result.returncode != 0
    assert bank.read_bytes() == b'edited after stage', 'rebase failure changed target before recovery was ready'
    assert staged_backup.read_bytes() == b'retail content' and manifest.exists(), 'rebase failure consumed recovery data'
    assert backup.read_bytes() == b'staged content'
    handle = lock(bank, 1)
    try:
        result = run(root, 'stage_failure_after_rebase', forget=True)
    finally:
        kernel.CloseHandle(handle)
    assert result.returncode != 0 and manifest.exists()
    assert bank.read_bytes() == b'edited after stage'
    assert backup.read_bytes() == staged_backup.read_bytes() == b'retail content'
    assert run(root, 'rebase_retry', forget=True).returncode == 0
    assert bank.read_bytes() == b'retail content'
    assert not backup.exists() and not staged_backup.exists() and not manifest.exists()
    assert not failures, '; '.join(failures)
    print('Restore comparison, failure diagnostics, retry and forget: PASS')


if __name__ == '__main__':
    main()
