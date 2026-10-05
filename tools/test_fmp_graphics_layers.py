#!/usr/bin/env python3
"""Verify named FMP meshes compose per entry with stable IDs and texture links."""
import argparse
import hashlib
import json
import shutil
import subprocess
import tempfile
import zipfile
from pathlib import Path

from test_fmp_texture_layers import entries


GRAPHICS = Path('data/graphics/graphics.big')
PACKAGES = ('Backflip.fmp', 'Hollow Mask Mesh - Care Package.fmp',
            'Half Mask (Variant 1).fmp', 'Lava JOB Mask.fmp',
            'Hellfirereaver Fix.fmp', 'Bow Light.fmp', 'Hollow Wip.fmp')


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def run(tool, *args, ok=True):
    result = subprocess.run([str(tool), *map(str, args)], capture_output=True, timeout=210)
    assert (result.returncode == 0) == ok, (args, result.stderr.decode(errors='replace'))
    return json.loads(result.stdout) if ok and '--json' in args else result


def check(output, expected, untouched, retail):
    built = entries(output/GRAPHICS, 'MBANK_ALLMESHES', set(expected) | {untouched})
    for name, source in {**expected, untouched: retail[untouched]}.items():
        assert name in built, name
        assert (built[name]['id'], built[name]['type'], built[name]['header'], built[name]['payload']) == (
            source['id'], source['type'], source['header'], source['payload']), name


def remove(path, root):
    target = path.resolve()
    assert target != root.resolve() and target.is_relative_to(root.resolve())
    if target.exists():
        shutil.rmtree(target)


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    parser.add_argument('--work-root', type=Path, default=repo/'build',
                        help='Parent for an owned temporary test directory')
    args = parser.parse_args()
    build = args.work_root.resolve()
    build.mkdir(parents=True, exist_ok=True)
    source_hashes = {'archive': digest(args.archive),
                     'graphics': digest(args.install/GRAPHICS),
                     'textures': digest(args.install/'data/graphics/pc/textures.big'),
                     'game.bin': digest(args.install/'data/CompiledDefs/game.bin')}
    with tempfile.TemporaryDirectory(prefix='fmp-graphics-layers-', dir=build) as temporary:
        work = Path(temporary).resolve()
        assert work != build and work.is_relative_to(build)
        with zipfile.ZipFile(args.archive) as archive:
            catalogue = {r['file']: r for r in json.loads(archive.read('catalogue.json'))}
            for name in PACKAGES:
                payload = archive.read('mods/' + name)
                assert hashlib.sha256(payload).hexdigest() == catalogue['mods/' + name]['sha256']
                (work/name).write_bytes(payload)
        packages = {name: entries(work/name, 'graphics') for name in PACKAGES}
        untouched = 'MESH_OBJECT_BARREL'
        target = 'MESH_HERO_JOB_MASK_01'
        retail = entries(args.install/GRAPHICS, 'MBANK_ALLMESHES', {untouched, target})
        assert untouched in retail

        direct = work/'direct'
        run(args.tool, 'fmp', 'apply', args.install, work/PACKAGES[1], direct)
        check(direct, packages[PACKAGES[1]], untouched, retail)
        remove(direct, work)

        run(args.tool, 'fmp', 'apply', args.install, work/PACKAGES[6], direct)
        check(direct, packages[PACKAGES[6]], untouched, retail)
        source_texture = entries(work/PACKAGES[6], 'maintextures')['HOLLOW_TEX1']
        built_texture = entries(direct/'data/graphics/pc/textures.big',
                                'GBANK_MAIN_PC', {'HOLLOW_TEX1'})['HOLLOW_TEX1']
        assert (built_texture['id'], built_texture['header'], built_texture['payload']) == (
            source_texture['id'], source_texture['header'], source_texture['payload'])
        remove(direct, work)

        disjoint = work/'disjoint'
        report = run(args.tool, 'mods', 'merge', args.install, disjoint, '--with',
                     work/PACKAGES[0], work/PACKAGES[1], '--json')
        assert not report['graphics']['conflicts']
        check(disjoint, {**packages[PACKAGES[0]], **packages[PACKAGES[1]]}, untouched, retail)
        remove(disjoint, work)

        assert packages[PACKAGES[2]][target]['id'] == packages[PACKAGES[3]][target]['id']
        for first, last in ((PACKAGES[2], PACKAGES[3]), (PACKAGES[3], PACKAGES[2])):
            output = work/'overlap'
            report = run(args.tool, 'mods', 'merge', args.install, output, '--with',
                         work/first, work/last, '--json')
            conflicts = report['graphics']['conflicts']
            assert len(conflicts) == 1 and conflicts[0]['entry'] == target
            assert conflicts[0]['winner'] == Path(last).stem
            check(output, packages[last], untouched, retail)
            remove(output, work)

        picks = work/'forge_mods_picks.txt'
        picks.write_text('mesh:' + target + '\t' + Path(PACKAGES[2]).stem + '\n')
        output = work/'picked'
        report = run(args.tool, 'mods', 'merge', args.install, output, '--with',
                     work/PACKAGES[2], work/PACKAGES[3], '--picks', picks, '--json')
        assert report['graphics']['conflicts'][0]['winner'] == Path(PACKAGES[2]).stem
        assert report['graphics']['conflicts'][0]['overridden']
        check(output, packages[PACKAGES[2]], untouched, retail)
        remove(output, work)

        picks.write_text('mesh:' + target + '\tvanilla\n')
        report = run(args.tool, 'mods', 'merge', args.install, output, '--with',
                     work/PACKAGES[2], work/PACKAGES[3], '--picks', picks, '--json')
        assert report['graphics']['conflicts'][0]['winner'] == 'vanilla'
        check(output, {target: retail[target]}, untouched, retail)
        remove(output, work)

        output.mkdir(); (output/'sentinel').write_bytes(b'keep prior output')
        for winner in ('MissingMod', Path(PACKAGES[0]).stem):
            picks.write_text('mesh:' + target + '\t' + winner + '\n')
            failed = run(args.tool, 'mods', 'merge', args.install, output, '--with',
                         work/PACKAGES[0], work/PACKAGES[2], work/PACKAGES[3],
                         '--picks', picks, '--json', ok=False)
            assert b'mods mesh choice:' in failed.stderr and b'review or clear' in failed.stderr
            assert winner.encode() in failed.stderr
            assert list(output.iterdir()) == [output/'sentinel']
            assert (output/'sentinel').read_bytes() == b'keep prior output'
        picks.write_text('mesh:UNREPORTED_ENTRY\tMissingMod\n')
        run(args.tool, 'mods', 'merge', args.install, output, '--with',
            work/PACKAGES[2], work/PACKAGES[3], '--picks', picks, '--json')
        check(output, packages[PACKAGES[3]], untouched, retail)
        assert (output/'sentinel').read_bytes() == b'keep prior output'
        remove(output, work)

        new_mesh = next(iter(packages[PACKAGES[1]]))
        picks.write_text('mesh:' + new_mesh + '\tvanilla\n')
        run(args.tool, 'mods', 'merge', args.install, output, '--with',
            work/PACKAGES[1], '--picks', picks, '--json')
        assert new_mesh not in entries(output/GRAPHICS, 'MBANK_ALLMESHES', {new_mesh})
        remove(output, work)

        for name, reason in ((PACKAGES[4], b'needs missing texture'),
                             (PACKAGES[5], b'already belongs to')):
            output = work/'refused'
            output.mkdir()
            sentinel = output/'sentinel.txt'
            sentinel.write_text('unchanged')
            failed = run(args.tool, 'mods', 'merge', args.install, output,
                         '--with', work/name, ok=False)
            assert reason in failed.stderr, failed.stderr.decode(errors='replace')
            assert sentinel.read_text() == 'unchanged'
            assert not (output/GRAPHICS).exists()
            remove(output, work)

    assert source_hashes == {'archive': digest(args.archive),
                             'graphics': digest(args.install/GRAPHICS),
                             'textures': digest(args.install/'data/graphics/pc/textures.big'),
                             'game.bin': digest(args.install/'data/CompiledDefs/game.bin')}
    print('Named FMP graphics replacement, addition, disjoint/overlap order and refusals pass')


if __name__ == '__main__':
    main()
