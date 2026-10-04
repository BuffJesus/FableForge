"""Test-only content overlays, explicitly excluding unsupported definition banks.

These fixtures exercise level/asset composition, never certify an installed mod.
"""
import json
from pathlib import Path
import shutil
import subprocess
from test_aeon_controller import inventory


def refuse_overhaul(tool, base, source, output, cwd, evidence):
    """Check the full source refuses without changing inputs or prior output."""
    base, source, output, evidence = map(Path, (base, source, output, evidence))
    output.mkdir(parents=True, exist_ok=True)
    marker = output/'prior-result.txt'
    if not marker.exists(): marker.write_bytes(b'preserve prior output')
    before = [inventory(path) for path in (base, source, output)]
    proc = subprocess.run([str(tool), 'mods', 'merge', str(base), str(output),
                           '--with', str(source)], cwd=cwd, capture_output=True, text=True, timeout=300)
    evidence.mkdir(parents=True, exist_ok=True)
    (evidence/'refusal.log').write_text(proc.stdout+proc.stderr, encoding='utf-8')
    assert proc.returncode != 0 and 'changed or new unnamed definition entries' in proc.stderr, proc.stderr
    assert [inventory(path) for path in (base, source, output)] == before
    (evidence/'result.json').write_text(json.dumps({'full_source_refused': True,
        'all_inputs_and_prior_output_preserved': True, 'gameplay_tested': False}, indent=2), encoding='utf-8')


def content_only(source, target):
    """A copied test tree for the retained level/asset checks, never a mod install."""
    source, target = Path(source), Path(target)
    assert not target.exists()
    shutil.copytree(source, target, ignore=shutil.ignore_patterns('CompiledDefs', 'compileddefs'))
    assert not (target/'data/CompiledDefs').exists()
    print('CONTENT-ONLY FIXTURE: definition banks excluded; not a complete mod installation:', target, flush=True)
    return target
