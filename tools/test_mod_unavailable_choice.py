#!/usr/bin/env python3
"""An unavailable saved winner must agree with composition and remain reviewable."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
from test_mod_field_details import write_bank


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='mod-unavailable-choice-', dir=repo/'build')).resolve()
    assert work.parent == (repo/'build').resolve()
    print('Evidence retained at', work, flush=True)
    root = work/'install'
    write_bank(root, (1, 2, 3, 4))
    sources = []
    def cli(label, *arguments):
        result = subprocess.run([str(repo/'build/forge-tools.exe'), *map(str, arguments)],
            cwd=repo, capture_output=True, text=True, timeout=60)
        (work/(label+'.log')).write_text(result.stdout+result.stderr, encoding='utf-8')
        assert result.returncode == 0, result.stdout+result.stderr
        return result.stdout
    for label in ('PackA', 'base', 'PackB'):
        pack = work/label
        (pack/'data/Misc').mkdir(parents=True)
        (pack/'forge_pack.json').write_text(json.dumps({'version': 1, 'name': label,
            'models': [], 'groundThemes': []}), encoding='utf-8')
        (pack/'data/Misc/shared.txt').write_text(label, encoding='utf-8')
        sources.append(pack)
        cli('add-'+label, 'mods', 'add', root, pack, '--name', label)
    picks = root/'forge_mods_picks.txt'
    original = b'file:data/misc/shared.txt\tRemovedPack With Long Weapon And Appearance Overrides\n'
    picks.write_bytes(original)
    baseline = {p: hashlib.sha256(p.read_bytes()).hexdigest()
        for source in (root, *sources) for p in source.rglob('*') if p.is_file()}
    report = json.loads(cli('conflicts', 'mods', 'conflicts', root, '--json'))
    assert report['files']['contested'][0]['winner'] == 'PackB'
    output = work/'built'
    cli('build', 'mods', 'build', root, output, '--json')
    assert (output/'data/Misc/shared.txt').read_bytes() == b'PackB'
    for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
        case = work/size
        case.mkdir()
        lines = ['wait_maps', 'wait_ready', f'set saveroot {root.as_posix()}',
            f'set uiscale {scale}', 'mods_tab 1', 'frames 2', 'mods_conflicts',
            'wait_mods', 'frames 2', 'assert_state mods_conflicts 1',
            'assert_state mods_picks 1', 'assert_state mods_first_winner PackB',
            'assert_state mods_unavailable_choices 1', 'assert_state mods_wins_0 0',
            'assert_state mods_losses_0 1', 'assert_state mods_wins_1 0',
            'assert_state mods_losses_1 1', 'assert_state mods_wins_2 1',
            'assert_state mods_losses_2 0', 'reveal mod_unavailable_choice_0', 'frames 3',
            'reveal input_mod_conflict_search', 'frames 3', 'click input_mod_conflict_search',
            'input_text removedpack', 'frames 3', 'assert_state mods_conflicts_matching 1',
            'reveal mod_unavailable_choice_0', 'frames 3', 'mouse_move viewport', 'frames 3',
            'assert_widget mod_unavailable_choice_0',
            'dump_widget mod_unavailable_choice_0',
            'clear_toasts', f'screenshot {(case/"unavailable.png").as_posix()}', 'quit']
        script = case/'unavailable.txt'
        script.write_text('\n'.join(lines)+'\n', encoding='utf-8')
        result = subprocess.run([str(repo/'build/FableForge.exe'), '--install', str(args.install),
            '--size', size, '--auto', str(script)], cwd=repo, capture_output=True, text=True,
            timeout=180, env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1'))
        (case/'output.log').write_text(result.stdout+result.stderr, encoding='utf-8')
        log = Path(str(script)+'.log').read_text(encoding='utf-8')
        assert result.returncode == 0 and 'RESULT PASS' in log, log[-4000:]
        bounds = re.search(r'widget_rect mod_unavailable_choice_0 ([\d.-]+) ([\d.-]+) ([\d.-]+) ([\d.-]+)',log)
        assert bounds, 'review-message rectangle not captured'
        left,top,right,bottom = map(float,bounds.groups())
        png = (case/'unavailable.png').read_bytes()
        assert png[:8] == b'\x89PNG\r\n\x1a\n'
        width,height = struct.unpack_from('>II',png,16)
        assert 0 <= left < right <= width and 0 <= top < bottom <= height
        assert bottom-top >= 20*scale, 'long review message did not wrap'
        assert picks.read_bytes() == original
        print(size, 'actual winner, unavailable-choice review and preserved choice: PASS', flush=True)
    assert all(hashlib.sha256(p.read_bytes()).hexdigest() == digest for p, digest in baseline.items())
    print('Composer output and source/baseline/choice bytes preserved: PASS', flush=True)
    recovery = work/'clear-choice.txt'
    recovery.write_text('\n'.join(['wait_maps','wait_ready',f'set saveroot {root.as_posix()}',
        'set uiscale 1.5','mods_tab 1','frames 2','mods_conflicts','wait_mods','frames 2',
        'assert_state mods_unavailable_choices 1','reveal mod_winner_0','frames 3',
        'reveal mod_winner_0','frames 3','click mod_winner_0','frames 2',
        'click mod_load_order_0','frames 2','assert_state mods_picks 0',
        'assert_state mods_unavailable_choices 0','assert_state mods_first_winner PackB',
        'mod_pick * base','frames 2','assert_state mods_first_winner base',
        'assert_state mods_wins_1 1','assert_state mods_losses_1 0',
        'assert_state mods_unavailable_choices 0','mod_pick * none','frames 2',
        'assert_state mods_first_winner PackB','assert_state mods_unavailable_choices 1',
        'mod_pick * -','frames 2',
        'assert_state mods_picks 0','assert_state mods_first_winner PackB','quit'])+'\n',encoding='utf-8')
    result = subprocess.run([str(repo/'build/FableForge.exe'),'--install',str(args.install),
        '--size','800x600','--auto',str(recovery)],cwd=repo,capture_output=True,text=True,
        timeout=180,env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1'))
    (work/'recovery.log').write_text(result.stdout+result.stderr,encoding='utf-8')
    log = Path(str(recovery)+'.log').read_text(encoding='utf-8')
    assert result.returncode == 0 and 'RESULT PASS' in log,log[-4000:]
    assert not picks.exists()
    picks.write_bytes(b'file:data/misc/shared.txt\tbase\n')
    report = json.loads(cli('base-source-report','mods','conflicts',root,'--json'))
    assert report['files']['contested'][0]['winner'] == 'base'
    base_output = work/'base-source-built'
    cli('base-source-build','mods','build',root,base_output,'--json')
    assert (base_output/'data/Misc/shared.txt').read_bytes() == b'base'
    picks.write_bytes(b'file:data/misc/shared.txt\tnone\n')
    report = json.loads(cli('unavailable-alias-report','mods','conflicts',root,'--json'))
    assert report['files']['contested'][0]['winner'] == 'PackB'
    picks.unlink()
    assert all(hashlib.sha256(p.read_bytes()).hexdigest() == digest
        for p,digest in baseline.items() if p != picks)
    print('Actual dropdown clear, ordinary base source, unavailable none and preserved sources: PASS',flush=True)


if __name__ == '__main__':
    main()
