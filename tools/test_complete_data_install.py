#!/usr/bin/env python3
"""Complete installations preserve supplied banks and assets, then undeploy exactly."""
import argparse, hashlib, json, shutil, struct, subprocess, tempfile
from pathlib import Path
from test_mod_field_details import write_bank
from test_mod_bank_refusal import snapshot


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='complete-data-install-', dir=repo/'build'))
    print('Evidence retained at', work, flush=True)
    base, source, out = [work/name for name in ('baseline', 'overhaul', 'built')]
    write_bank(base, (1, 2, 3, 4), record='')
    write_bank(source, (91, 92, 93, 94), record='')
    for root in (base, source):
        for bank in ('script.bin', 'frontend.bin'):
            shutil.copyfile(root/'data/CompiledDefs/game.bin', root/'data/CompiledDefs'/bank)
        (root/'data/Misc').mkdir()
        (root/'data/Misc/shared.asset').write_bytes(root.name.encode())
    (base/'data/Misc/baseline-only.asset').write_bytes(b'baseline')
    (source/'data/Misc/new.asset').write_bytes(b'overhaul content')
    out.mkdir(); (out/'prior.txt').write_bytes(b'prior')
    original_source = snapshot(source)
    checks = []
    def run(label, *argv, error=None):
        prior = snapshot(out)
        result = subprocess.run([str(args.tool.resolve()), *map(str, argv)],
                                capture_output=True, timeout=120, cwd=repo)
        (work/(label+'.log')).write_bytes(result.stdout+result.stderr)
        if error:
            assert result.returncode != 0 and error.encode() in result.stderr, (label, result.stderr)
            assert snapshot(out) == prior, label
        else:
            assert result.returncode == 0, (label, result.stderr)
        checks.append(label)
        return result.stdout
    run('composition-refuses-anonymous', 'mods', 'merge', base, out, '--with', source,
        error='unnamed definition entries')
    report = json.loads(run('complete-install', 'mods', 'merge', base, out,
                           '--install-data', '--with', source/'data', '--json'))
    assert report['complete_data']['unnamed_records'] == 1
    assert len(report['sources']) == 1 and isinstance(report['sources'][0], dict)
    for rel, digest in original_source.items():
        assert hashlib.sha256((out/rel).read_bytes()).hexdigest() == digest, rel
    assert not (out/'data/Levels/FinalAlbion.wad').exists()
    run('register-data-alias', 'mods', 'add', base, source/'data', '--install-data', '--name', 'Overhaul')
    listed = json.loads(run('persist-mode', 'mods', 'list', base, '--json'))
    assert listed['mods'][0]['install_data'] and Path(listed['mods'][0]['source']) == source
    baseline = snapshot(base)
    run('deploy', 'mods', 'deploy', base, '--json')
    for rel, digest in original_source.items():
        assert hashlib.sha256((base/rel).read_bytes()).hexdigest() == digest, rel
    assert (base/'data/Misc/baseline-only.asset').read_bytes() == b'baseline'
    deployed = snapshot(base)
    run('check-deployed', 'mods', 'conflicts', base, '--json')
    assert snapshot(base) == deployed
    run('redeploy', 'mods', 'deploy', base, '--json')
    assert snapshot(base) == deployed
    run('undeploy', 'mods', 'undeploy', base)
    assert snapshot(base) == baseline
    # A minimal independently written archive checks packed installation without
    # relying on the production writer for its input or a large game fixture.
    levels = base/'data/Levels'; levels.mkdir()
    old_payload = b'original level payload'
    name = b'Data\\Levels\\FinalAlbion\\original.lev\0'
    raw = bytearray(2048)
    struct.pack_into('<I', raw, 20, 1)
    struct.pack_into('<I', raw, 28, 2048+len(old_payload))
    raw += old_payload + struct.pack('<I', 0)
    raw += struct.pack('<7I', 42, 0, 1, len(old_payload), 2048, 0, len(name))+name
    raw += struct.pack('<3I', 0, 0, 0)
    (levels/'FinalAlbion.wad').write_bytes(raw)
    authored = source/'data/Levels/FinalAlbion'; authored.mkdir(parents=True)
    (authored/'original.lev').write_bytes(b'overhaul level payload')
    (authored/'new.lev').write_bytes(b'new overhaul level')
    packed_report = json.loads(run('packed-install', 'mods', 'merge', base, out,
                                  '--install-data', '--with', source, '--json'))
    assert packed_report['complete_data']['wad_repacked'] == 1
    assert packed_report['complete_data']['wad_appended'] == 1
    extracted = work/'extracted'
    run('extract-packed', 'wad', 'extract', out/'data/Levels/FinalAlbion.wad', extracted)
    for level in authored.iterdir():
        assert (extracted/'Data/Levels/FinalAlbion'/level.name).read_bytes() == level.read_bytes()
    original_source = snapshot(source)
    runtime = work/'FableControllerSupport'; (runtime/'Data').mkdir(parents=True)
    (runtime/'FableControllerSupport.dll').write_bytes(b'fixture runtime, never loaded')
    (runtime/'Data/replace.asset').write_bytes(b'avoid this incomplete installation')
    run('controller-missing-fmp', 'mods', 'merge', base, out, '--install-data',
        '--with', source, runtime, error='also needs its legacy ControllerSupport.fmp')
    incomplete = work/'incomplete'
    shutil.copytree(source, incomplete)
    (incomplete/'data/CompiledDefs/frontend.bin').unlink()
    run('missing-bank', 'mods', 'merge', base, out, '--install-data', '--with', incomplete,
        error='missing its compiled banks')
    partial = work/'partial'; (partial/'data/Misc').mkdir(parents=True)
    (partial/'data/Misc/extra.asset').write_bytes(b'partial')
    run('insert-before-overhaul', 'mods', 'add', base, partial, '--at', '0')
    run('wrong-order', 'mods', 'build', base, out, error='first enabled mod')
    assert snapshot(source) == original_source
    (work/'result.json').write_text(json.dumps({'passed': True, 'checks': checks,
                                               'gameplay_tested': False}, indent=2))
    print('PASS:', len(checks), 'complete installation checks', flush=True)


if __name__ == '__main__':
    main()
