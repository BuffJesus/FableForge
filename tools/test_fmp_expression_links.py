#!/usr/bin/env python3
"""Check original weapon-expression packages against actual HUD texture IDs."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import zipfile

from test_fmp_links import package_records, read_bank
from test_fmp_texture_layers import entries
from test_fmp_weapon_links import run, field_start


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', required=True, type=Path)
    parser.add_argument('--install', required=True, type=Path)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    args = parser.parse_args()
    textures = args.install/'data/graphics/pc/textures.big'
    text_file = args.install/'data/lang/English/text.big'
    inputs = [args.archive, textures, text_file, args.install/'data/CompiledDefs/game.bin',
              args.install/'data/CompiledDefs/names.bin']
    before = {str(p): digest(p) for p in inputs}
    work = Path(tempfile.mkdtemp(prefix='fmp-expression-links-', dir=repo/'build'))
    print('Evidence retained at', work, flush=True)
    checks = []
    with zipfile.ZipFile(args.archive) as archive:
        catalogue = {r['file']: r for r in json.loads(archive.read('catalogue.json'))}
        for index, name in enumerate(('Expressions With Weapons',
                                     'Hold weapon on some expressions',
                                     'Hold weapon with Bloodlust Roar')):
            member = f'mods/{name}.fmp'
            payload = archive.read(member)
            assert hashlib.sha256(payload).hexdigest() == catalogue[member]['sha256']
            package = work/f'{index}.fmp'; package.write_bytes(payload)
            source = package_records(package)['GameBINEntries']
            report = json.loads(run(args.tool, 'fmp', 'links', package, '--groups', '--json').stdout)
            icons = [(r['record'], g['targets'][0]) for r in report['records']
                     for g in r['groups'] if g['path'] == '222402816.ExpressionHudIconID'
                     and len(g['targets']) == 1]
            assert icons, 'original must exercise expression icon references'
            text_tags = [(r['record'], g['targets'][0]) for r in report['records']
                         for g in r['groups'] if g['path'] == '4192721254.TEXT_EXPRESSION_ID'
                         and len(g['targets']) == 1]
            identities = entries(textures, 'GBANK_MAIN_PC', {n for _, n in icons},
                                 include_payload=False)
            text_ids = entries(text_file, 'TEXT_ENGLISH_MAIN', {n for _, n in text_tags},
                               include_payload=False)
            direct, merged = work/f'direct-{index}', work/f'merged-{index}'
            run(args.tool, 'fmp', 'apply', args.install, package, direct)
            run(args.tool, 'mods', 'merge', args.install, merged, '--with', package)
            for bank in ('game.bin', 'names.bin'):
                assert digest(direct/'data/CompiledDefs'/bank) == digest(merged/'data/CompiledDefs'/bank)
            built = {e['name']: e['data'] for e in read_bank(direct) if e['name']}
            for record, target in icons:
                at = field_start(source[record][0], '0d419900')
                assert struct.unpack_from('<I', built[record], at)[0] == identities[target]['id']
            for record, target in text_tags:
                at = field_start(source[record][0], 'f9e7d966')
                assert struct.unpack_from('<I', built[record], at)[0] == text_ids[target]['id']
            checks.append({'file': member, 'icons': len(icons), 'text_tags': len(text_tags)})
    assert before == {str(p): digest(p) for p in inputs}
    (work/'result.json').write_text(json.dumps({'checks': checks, 'inputs_unchanged': True,
                                              'gameplay_tested': False}, indent=2))
    print('PASS:', len(checks), 'original expression packages; direct and composed HUD references')


if __name__ == '__main__':
    main()
