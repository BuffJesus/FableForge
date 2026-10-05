#!/usr/bin/env python3
"""Refuse unavailable whole-file winners instead of silently rebinding saved choices."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
from test_mod_field_details import write_bank


def snapshot(root):
    return {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in root.rglob('*') if p.is_file()}


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work-root', type=Path, default=repo/'build')
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    args = parser.parse_args()
    args.work_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='mod-file-choice-contributors-', dir=args.work_root))
    print('Evidence retained at', work, flush=True)
    root = work/'baseline'; write_bank(root, (1, 2, 3, 4))
    relative = Path('data/Misc/shared.bin')
    (root/relative).parent.mkdir(parents=True); (root/relative).write_bytes(b'baseline')
    sources = [work/name for name in ('PackA', 'PackB', 'PackC')]
    for source in sources:
        (source/relative).parent.mkdir(parents=True)
    (sources[0]/relative).write_bytes(b'first'); (sources[1]/relative).write_bytes(b'last')
    (sources[2]/'data/Misc/other.bin').write_bytes(b'unrelated contributor')

    def run(*arguments, good=True):
        result = subprocess.run([str(args.tool), *map(str, arguments)], capture_output=True,
                                text=True, timeout=120)
        assert (result.returncode == 0) == good, (arguments, result.stderr)
        return result

    for source in sources:
        run('mods', 'add', root, source)
    before_inputs = {str(p): snapshot(p) for p in [root, *sources]}
    picks = work/'choices.txt'; checks = []
    for route in ('merge', 'build'):
        output = work/route
        command = ['mods', route, root, output]
        if route == 'merge': command += ['--with', *sources]
        run(*command)
        assert (output/relative).read_bytes() == b'last'
        (output/'sentinel').write_bytes(b'prior output')
        chosen = command + ['--picks', picks]
        for winner, data in (('PackA', b'first'), ('vanilla', b'baseline')):
            picks.write_text('file:data/misc/shared.bin\t'+winner+'\n')
            run(*chosen); assert (output/relative).read_bytes() == data
            checks.append({'route': route, 'case': winner, 'result': 'selected exact source'})
        for failure in ('removed label', 'unrelated enabled source', 'source now baseline', 'source file removed'):
            winner = 'Gone' if failure == 'removed label' else 'PackC' if failure == 'unrelated enabled source' else 'PackA'
            if failure == 'source now baseline': (sources[0]/relative).write_bytes(b'baseline')
            if failure == 'source file removed': (sources[0]/relative).unlink()
            picks.write_text('file:data/misc/shared.bin\t'+winner+'\n')
            before = snapshot(output)
            result = run(*chosen, good=False)
            assert 'mods file choice:' in result.stderr and 'review or clear' in result.stderr
            assert winner in result.stderr and snapshot(output) == before
            (work/(route+'-'+failure.replace(' ', '-')+'.log')).write_text(result.stderr)
            (sources[0]/relative).write_bytes(b'first')
            checks.append({'route': route, 'case': failure, 'result': 'refused; prior files preserved'})
        picks.write_text('file:data/misc/temporarily_unreported.bin\tGone\n')
        run(*chosen); assert (output/relative).read_bytes() == b'last'
        assert not (output/'data/Misc/temporarily_unreported.bin').exists()
        checks.append({'route': route, 'case': 'inactive choice', 'result': 'retained without rebinding'})
        picks.write_text('')
        run(*chosen); assert (output/relative).read_bytes() == b'last'
        assert (output/'sentinel').read_bytes() == b'prior output'
    assert before_inputs == {str(p): snapshot(p) for p in [root, *sources]}
    (work/'result.json').write_text(json.dumps({'checks': checks, 'inputs_unchanged': True}, indent=2))
    print('PASS:', len(checks), 'direct/build source-choice and refusal cases')


if __name__ == '__main__':
    main()
