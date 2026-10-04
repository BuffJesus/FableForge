#!/usr/bin/env python3
"""Verify named FMP textures compose per entry and follow load order."""
import argparse
import hashlib
import json
import mmap
import shutil
import struct
import subprocess
import tempfile
import zipfile
from pathlib import Path


PACKAGES = ('Young Hero Retexture.fmp', 'Apprentice outfit recolour.fmp',
            'Apprentice Retext.fmp', 'Blue Ass Beta Textures.fmp',
            "Rhadiel's Bump Maps.fmp")
TEXTURES = Path('data/graphics/pc/textures.big')


def entries(path, bank_name, wanted=None, include_payload=True):
    """Independent BIG table walk; copy payloads only for requested entries."""
    found = {}
    with path.open('rb') as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
        assert data[:4] in (b'BIGB', b'B\0\0\0')
        footer = struct.unpack_from('<I', data, 8)[0]
        count = struct.unpack_from('<I', data, footer)[0]
        cursor = footer + 4
        for _ in range(count):
            end = data.find(b'\0', cursor)
            name = data[cursor:end].decode('ascii')
            cursor = end + 1
            _, size, start, _, _ = struct.unpack_from('<5I', data, cursor)
            cursor += 20
            if name != bank_name:
                continue
            at = start + 4 + struct.unpack_from('<I', data, start)[0] * 8
            for _ in range(size):
                record_at = at
                _, entry_id, kind, length, offset, _, name_size = struct.unpack_from('<7I', data, at)
                at += 28
                symbol = data[at:at+name_size].rstrip(b'\0').decode('ascii')
                at += name_size + 4
                source_count = struct.unpack_from('<I', data, at)[0]
                at += 4
                for _ in range(source_count):
                    n = struct.unpack_from('<I', data, at)[0]
                    at += 4 + n
                header_size = struct.unpack_from('<I', data, at)[0]
                at += 4
                header = data[at:at+header_size]
                at += header_size
                if wanted is None or symbol in wanted:
                    if symbol in found:
                        raise ValueError('duplicate BIG entry name: ' + symbol)
                    found[symbol] = {'id': entry_id, 'type': kind, 'header': header,
                                     'length': length,
                                     'payload': data[offset:offset+length] if include_payload else None,
                                     'record_offset': record_at}
            break
    return found


def run(tool, *args, ok=True):
    result = subprocess.run([str(tool), *map(str, args)], capture_output=True, timeout=180)
    assert (result.returncode == 0) == ok, (args, result.stderr.decode(errors='replace'))
    return json.loads(result.stdout) if ok and '--json' in args else result


def check_output(output, expected, untouched):
    built = entries(output/TEXTURES, 'GBANK_MAIN_PC', set(expected) | {untouched})
    for name, source in expected.items():
        assert name in built, name
        assert (built[name]['id'], built[name]['type'], built[name]['header'], built[name]['payload']) == (
            source['id'], source['type'], source['header'], source['payload']), name


def checked_remove(path, root):
    path = path.resolve()
    assert path != root.resolve() and path.is_relative_to(root.resolve())
    if path.exists():
        shutil.rmtree(path)


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    args = parser.parse_args()
    build = (repo/'build').resolve()
    source_hashes = {'archive': digest(args.archive),
                     'textures': digest(args.install/TEXTURES),
                     'game.bin': digest(args.install/'data/CompiledDefs/game.bin')}
    with tempfile.TemporaryDirectory(prefix='fmp-texture-layers-', dir=build) as temporary:
        work = Path(temporary).resolve()
        assert work != build and work.is_relative_to(build)
        with zipfile.ZipFile(args.archive) as archive:
            catalogue = {r['file']: r for r in json.loads(archive.read('catalogue.json'))}
            for name in PACKAGES:
                payload = archive.read('mods/' + name)
                assert hashlib.sha256(payload).hexdigest() == catalogue['mods/' + name]['sha256']
                (work/name).write_bytes(payload)
        packages = {name: entries(work/name, 'maintextures') for name in PACKAGES}
        untouched = 'YOUNGHERO_SKIN_FACE'
        retail_apprentice = 'TEXTURE_HERO_APPRENTICE_SHIRT_NEUTRAL'
        retail = entries(args.install/TEXTURES, 'GBANK_MAIN_PC',
                         {untouched, retail_apprentice})
        assert untouched in retail

        direct = work/'direct'
        run(args.tool, 'fmp', 'apply', args.install, work/PACKAGES[0], direct)
        check_output(direct, {**packages[PACKAGES[0]], untouched: retail[untouched]}, untouched)

        masked_output = work/'masked'
        report = run(args.tool, 'mods', 'merge', args.install, masked_output, '--with',
                     work/PACKAGES[1], direct, '--json')
        assert report['maintextures']['masked'] == [
            {'mod': Path(PACKAGES[1]).stem, 'by': 'direct',
             'entries': len(packages[PACKAGES[1]])}]
        check_output(masked_output, {**packages[PACKAGES[0]],
                                     retail_apprentice: retail[retail_apprentice],
                                     untouched: retail[untouched]}, untouched)
        checked_remove(masked_output, work)

        later_output = work/'later-fmp'
        report = run(args.tool, 'mods', 'merge', args.install, later_output, '--with',
                     direct, work/PACKAGES[1], '--json')
        assert not report['maintextures']['masked']
        check_output(later_output, {**packages[PACKAGES[0]], **packages[PACKAGES[1]],
                                    untouched: retail[untouched]}, untouched)
        checked_remove(later_output, work)
        checked_remove(direct, work)

        mixed = work/'mixed'
        report = run(args.tool, 'mods', 'merge', args.install, mixed, '--with',
                     work/PACKAGES[0], work/PACKAGES[1], '--json')
        assert not report['maintextures']['conflicts']
        assert report['summary']['maintextures_conflicts'] == 0
        check_output(mixed, {**packages[PACKAGES[0]], **packages[PACKAGES[1]],
                             untouched: retail[untouched]}, untouched)
        checked_remove(mixed, work)

        added = work/'added'
        run(args.tool, 'fmp', 'apply', args.install, work/PACKAGES[3], added)
        check_output(added, {**packages[PACKAGES[3]], untouched: retail[untouched]}, untouched)
        base_count = next(b['count'] for b in run(args.tool, 'big', 'list',
            args.install/TEXTURES, '--json')['banks'] if b['bank'] == 'GBANK_MAIN_PC')
        added_count = next(b['count'] for b in run(args.tool, 'big', 'list',
            added/TEXTURES, '--json')['banks'] if b['bank'] == 'GBANK_MAIN_PC')
        assert added_count == base_count + len(packages[PACKAGES[3]])
        checked_remove(added, work)

        for order in ((PACKAGES[1], PACKAGES[2]), (PACKAGES[2], PACKAGES[1])):
            output = work/'overlap'
            report = run(args.tool, 'mods', 'merge', args.install, output, '--with',
                         *(work/name for name in order), '--json')
            assert len(report['maintextures']['conflicts']) == 2
            assert report['summary']['maintextures_conflicts'] == 2
            assert all(c['winner'] == Path(order[-1]).stem for c in report['maintextures']['conflicts'])
            check_output(output, {**packages[order[0]], **packages[order[1]],
                                  untouched: retail[untouched]}, untouched)
            checked_remove(output, work)

        # A package whose named texture claims another numeric id must refuse
        # before changing a requested output directory.
        bad = bytearray((work/PACKAGES[0]).read_bytes())
        sample = packages[PACKAGES[0]]['YOUNGHERO_HAIR']
        struct.pack_into('<I', bad, sample['record_offset'] + 4, sample['id'] + 1)
        invalid = work/'wrong-id.fmp'; invalid.write_bytes(bad)
        refused = work/'refused'; refused.mkdir()
        (refused/'prior.txt').write_bytes(b'keep prior output')
        run(args.tool, 'mods', 'merge', args.install, refused, '--with', invalid, '--json', ok=False)
        assert [p.relative_to(refused).as_posix() for p in refused.rglob('*') if p.is_file()] == ['prior.txt']
        real_refusal = run(args.tool, 'mods', 'merge', args.install, refused,
                           '--with', work/PACKAGES[4], '--json', ok=False)
        assert 'FMP maintextures: id' in real_refusal.stderr.decode(errors='replace')
        assert [p.relative_to(refused).as_posix() for p in refused.rglob('*') if p.is_file()] == ['prior.txt']
        ego = work/'EgoTexture'
        resource = ego/'Data/graphics/pc/textures.big/GBANK_MAIN_PC/YOUNGHERO_HAIR.resource'
        resource.parent.mkdir(parents=True)
        resource.write_bytes(b'probe only')
        (ego/'EgoTexture.dll').write_bytes(b'probe only')
        mixed_refusal = run(args.tool, 'mods', 'merge', args.install, refused,
                            '--with', ego, work/PACKAGES[0], '--json', ok=False)
        assert 'earlier EgoCore .resource' in mixed_refusal.stderr.decode(errors='replace')
        assert [p.relative_to(refused).as_posix() for p in refused.rglob('*') if p.is_file()] == ['prior.txt']
        assert source_hashes == {'archive': digest(args.archive),
                                 'textures': digest(args.install/TEXTURES),
                                 'game.bin': digest(args.install/'data/CompiledDefs/game.bin')}
        print('FMP texture replacement/addition, disjoint merge, whole-file order, conflict orders and ID/mixed-resource refusals pass')


if __name__ == '__main__':
    main()
