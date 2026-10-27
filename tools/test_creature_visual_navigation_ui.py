#!/usr/bin/env python3
"""Read-only native body/appearance navigation and draft protection at both sizes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
from audit_creature_visual_graph import analyze
from test_definition_edit_ui import click
from test_fmp_links import read_bank


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--gui', type=Path, default=repo/'build/FableForge.exe')
    parser.add_argument('--cwd', type=Path, default=repo)
    parser.add_argument('--work-root', type=Path, default=repo/'build')
    args = parser.parse_args()
    native = args.install.resolve()
    assert not args.work_root.resolve().is_relative_to(native)
    inputs = sorted((native/'data/CompiledDefs').glob('*.bin'))
    assert inputs
    def hashes():
        return {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
    before = hashes()
    records = read_bank(native)
    candidates = [row for row in analyze(records)['records'] if not row['issues']]
    roots = [next(row for row in candidates if all(v['owner_is_donor'] for v in row['visual_rows'])),
             next(row for row in candidates if any(not v['owner_is_donor'] for v in row['visual_rows']))]
    work = Path(tempfile.mkdtemp(prefix='creature-visual-navigation-ui-', dir=args.work_root.resolve()))
    print('Evidence retained at', work, flush=True)
    jumps = guards = 0
    for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
        case = work/size
        case.mkdir()
        lines = ['wait_maps', 'wait_ready', f'set uiscale {scale}', 'assets_tab 6', 'wait_definitions']
        for root in roots:
            # Select by the actual name; link destinations must be the independently
            # parsed CDefs index, not a name lookup or OriginalParent index.
            for visual, widget in zip(root['visual_rows'], ('btn_definition_body', 'btn_definition_appearance')):
                lines += [f'definition_select {root["name"]}', 'frames 3']
                lines += click(widget) + [f'assert_state definition_selected {visual["component"]}']
                jumps += 1
            lines += [f'definition_select {root["name"]}', 'frames 3', 'definition_clone',
                      'assert_state definition_editing 1']
            for widget in ('btn_definition_body', 'btn_definition_appearance'):
                lines += click(widget) + [f'assert_state definition_selected {root["index"]}',
                                         'assert_state definition_editing 1']
                guards += 1
            lines += ['definition_cancel', 'assert_state definition_editing 0']
        lines += [f'definition_select {roots[1]["name"]}', 'frames 3']
        lines += click('btn_definition_body') + [f'screenshot {(case/"body.png").as_posix()}', 'quit']
        script = case/'navigation.txt'
        script.write_text('\n'.join(lines)+'\n', encoding='utf-8')
        result = subprocess.run([str(args.gui.resolve()), '--install', str(native), '--size', size,
                                 '--auto', str(script)], cwd=args.cwd.resolve(),
                                env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1',
                                         APPDATA=str(case/'profile'), LOCALAPPDATA=str(case/'local')),
                                capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=120)
        (case/'output.log').write_text(result.stdout+result.stderr, encoding='utf-8')
        trace = Path(str(script)+'.log').read_text(encoding='utf-8')
        assert result.returncode == 0 and 'RESULT PASS' in trace and 'FAIL ' not in trace, trace[-7000:]
        assert hashes() == before, 'source banks changed'
        print(size, 'own/inherited component links and draft guards: PASS', flush=True)
    (work/'result.json').write_text(json.dumps(dict(passed=True, target_jumps=jumps, draft_guards=guards,
                                                  roots=roots, source_hashes=before), indent=2)+'\n', encoding='utf-8')


if __name__ == '__main__':
    main()
