#!/usr/bin/env python3
"""Smoke-test an extracted local package from an unrelated working directory."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile
from urllib.parse import unquote


def verify_package_identity(package):
    manifest = package / 'PACKAGE_INFO.json'
    if not manifest.exists():
        return None  # Older validated archives predate package identities.
    info = json.loads(manifest.read_text(encoding='utf-8'))
    assert info['format'] == 1
    assert package.name == f"FableForge-{info['version']}-win64"
    actual = {p.relative_to(package).as_posix(): p for p in package.rglob('*')
              if p.is_file() and p != manifest}
    assert set(info['files']) == set(actual), 'package inventory differs from manifest'
    for name, path in actual.items():
        data = path.read_bytes()
        assert len(data) == info['files'][name]['bytes'], name
        assert hashlib.sha256(data).hexdigest() == info['files'][name]['sha256'], name
    return len(actual)


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('--root', type=Path, default=Path(os.environ.get('FABLE_ROOT',
        r'C:\Programs\Steam\steamapps\common\Fable The Lost Chapters')))
    parser.add_argument('--corpus', type=Path, default=repo/'work/nexus_mods/_peek')
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument('--start-at', help='Run this named workflow and subsequent workflows; earlier checks are reported as not run')
    selection.add_argument('--only', nargs='+', help='Run only these named workflows; all others are reported as not run')
    args = parser.parse_args()
    archive = args.archive.resolve()
    work = Path(tempfile.mkdtemp(prefix='package-smoke-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    unpack = work / 'package %FORGE_PACKAGE_TOKEN% & files'
    unpack.mkdir()
    with zipfile.ZipFile(archive) as source:
        assert source.testzip() is None, 'archive CRC failure'
        for member in source.infolist():
            target = (unpack / member.filename).resolve()
            assert target.is_relative_to(unpack), member.filename
        source.extractall(unpack)
    packages = [path for path in unpack.iterdir() if path.is_dir()]
    assert len(packages) == 1
    package = packages[0]
    identity_files = verify_package_identity(package)
    if identity_files is not None:
        print('packaged file identities: PASS', identity_files, flush=True)
    for name in ('FableForge.exe', 'forge.exe', 'forge-tools.exe', 'README.md', 'LICENSE',
                 'docs/re_reference/def_schema.json', 'presets'):
        assert (package / name).exists(), name
    pending = [package / name for name in ('README.md', 'FIRST_LEVEL.md', 'EDITOR.md',
        'ENGINE_RULES.md', 'AUTOMATION.md', 'THIRD_PARTY.md')]
    visited, broken, links = set(), [], 0
    while pending:
        document = pending.pop()
        if document in visited: continue
        visited.add(document)
        for link in re.findall(r'\]\(([^)]+)\)', document.read_text(encoding='utf-8')):
            link = link.split('#')[0]
            if not link or re.match(r'[A-Za-z]+:', link): continue
            links += 1
            target = (document.parent / unquote(link)).resolve()
            if not target.is_file() or not target.is_relative_to(package):
                broken.append({'document': str(document.relative_to(package)), 'target': link})
            elif target.suffix == '.md': pending.append(target)
    (work / 'doc-links.json').write_text(json.dumps({'checked': links, 'broken': broken}, indent=2), encoding='utf-8')
    assert not broken, broken
    guide = package / 'AEON_CONTROLLER.html'
    assert guide.is_file()
    pictures = re.findall(r'<img[^>]+src="([^"]+)"', guide.read_text(encoding='utf-8'))
    assert len(pictures) == 7
    for picture in pictures:
        target = (package / picture).resolve()
        assert target.is_relative_to(package) and target.is_file(), picture
    print('packaged documentation links: PASS', links, flush=True)
    cwd = work / 'unrelated working directory'
    cwd.mkdir()
    env = dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1', FORGE_PACKAGE_TOKEN='expanded')
    checks, not_run = [], []
    pending_start = args.start_at
    pending_only = set(args.only or [])
    with archive.open('rb') as stream:
        archive_hash = hashlib.file_digest(stream, 'sha256').hexdigest()
    def run(label, command):
        nonlocal pending_start
        if args.only is not None:
            if label not in pending_only:
                not_run.append(label)
                return
            pending_only.remove(label)
        if pending_start and label != pending_start:
            not_run.append(label)
            return
        pending_start = None
        result = subprocess.run(list(map(str, command)), cwd=cwd, env=env,
            capture_output=True, text=True, timeout=600)
        (work / (label + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        checks.append({'check': label, 'returncode': result.returncode})
        (work / 'progress.json').write_text(json.dumps({'archive': str(archive),
            'sha256': archive_hash, 'checks': checks, 'not_run': not_run}, indent=2), encoding='utf-8')
        assert result.returncode == 0, result.stdout[-2000:] + result.stderr[-2000:]
        print(label + ': PASS', flush=True)
    run('defs', [package / 'forge-tools.exe', 'defs', 'list', args.root.resolve(), 'game.bin', 'OBJECT_HERO'])
    run('mod_bank_refusal', [sys.executable, repo/'tools/test_mod_bank_refusal.py',
        '--tool', package/'forge-tools.exe', '--cwd', cwd])
    for label, options in [('mod_file_choice_reviews', []),
                           ('mod_file_choice_reviews_complete', ['--complete-data'])]:
        run(label, [sys.executable, repo/'tools/test_mod_file_choice_reviews.py',
            '--tool', package/'forge-tools.exe', '--work-root', work, *options])
    for family in ('field', 'record', 'fse', 'text', 'tng', 'qst'):
        for suffix, options in [('', []), ('_complete', ['--complete-data'])]:
            run(f'mod_{family}_choice_contributors{suffix}',
                [sys.executable, repo/f'tools/test_mod_{family}_choice_contributors.py',
                 '--tool', package/'forge-tools.exe', '--work-root', work, *options])
    for suffix, options in [('', []), ('_complete', ['--complete-data'])]:
        run('mod_text_payload_refusal' + suffix,
            [sys.executable, repo/'tools/test_mod_text_payload_refusal.py',
             '--tool', package/'forge-tools.exe', '--work-root', work, *options])
        run('mod_text_unnamed_refusal' + suffix,
            [sys.executable, repo/'tools/test_mod_text_unnamed_refusal.py',
             '--tool', package/'forge-tools.exe', '--work-root', work, *options])
        run('mod_text_metadata_refusal' + suffix,
            [sys.executable, repo/'tools/test_mod_text_metadata_refusal.py',
             '--tool', package/'forge-tools.exe', '--work-root', work, *options])
        run('mod_text_codec_refusal' + suffix,
            [sys.executable, repo/'tools/test_mod_text_codec_refusal.py',
             '--tool', package/'forge-tools.exe', '--work-root', work, *options])
    run('mod_staged_baseline_projection',
        [sys.executable, repo/'tools/test_mod_staged_baseline_projection.py',
         '--tool', package/'forge-tools.exe', '--work-root', work])
    run('mod_whole_file_winner_ui',
        [sys.executable, repo/'tools/test_mod_whole_file_winner_ui.py',
         '--tool', package/'forge-tools.exe', '--gui', package/'FableForge.exe',
         '--install', args.root.resolve(), '--work-root', work])
    for suffix, options in [('', ['--gui', package/'FableForge.exe', '--install', args.root.resolve()]),
                            ('_complete', ['--complete-data'])]:
        run('mod_level_case_identity' + suffix,
            [sys.executable, repo/'tools/test_mod_level_case_identity.py',
             '--tool', package/'forge-tools.exe', '--work-root', work, *options])
        run('mod_tng_identical_contributors' + suffix,
            [sys.executable, repo/'tools/test_mod_tng_identical_contributors.py',
             '--tool', package/'forge-tools.exe', '--work-root', work, *options])
    run('fmp_record_refusal', [sys.executable, repo/'tools/test_fmp_record_refusal.py',
        '--tool', package/'forge-tools.exe', '--cwd', cwd])
    run('retail', [sys.executable, repo / 'tools/retail_smoke.py', '--exe', package / 'forge.exe',
        '--count', '8', '--install', args.root.resolve(), '--out', work / 'exports'])
    run('gui', [sys.executable, repo / 'tools/ui_smoke.py', '--exe', package / 'FableForge.exe',
        '--install', args.root.resolve(), '--cwd', cwd])
    run('dialogue_workspace', [sys.executable, repo / 'tools/test_dialogue_workspace.py',
        '--gui', package / 'FableForge.exe', '--install', args.root.resolve(), '--cwd', cwd])
    run('mod_paths', [sys.executable, repo / 'tools/test_mod_gui_paths.py', '--gui', package / 'FableForge.exe',
        '--root', args.root.resolve(), '--cwd', cwd])
    for mode in ('redeploy', 'report', 'missing', 'unnamed', 'sibling', 'record', 'text', 'text-unnamed', 'text-metadata', 'text-codec'):
        run('mod_action_' + mode, [sys.executable, repo / 'tools/test_mod_action_error_ui.py',
            '--gui', package / 'FableForge.exe', '--install', args.root.resolve(), '--mode', mode])
    skipped = []
    corpus = args.corpus.resolve()
    controller_fmp = corpus/'ControllerSupportFMP/ControllerSupport.fmp'
    aeon = corpus/'AeonEdition'
    if controller_fmp.is_file() and (aeon/'Data/CompiledDefs/game.bin').is_file():
        run('fmp_named_links', [sys.executable, repo/'tools/test_fmp_links.py', '--tool',
            package/'forge-tools.exe', '--cwd', cwd, '--install', args.root.resolve(),
            '--fmp', controller_fmp, '--aeon', aeon])
        run('aeon_controller_preservation', [sys.executable, repo/'tools/test_aeon_controller.py',
            '--tool', package/'forge-tools.exe', '--cwd', cwd, '--install', args.root.resolve(),
            '--corpus', corpus, '--keep'])
    else: skipped.append('fmp_named_links: controller/Aeon corpus unavailable')
    f2_fmp = corpus/'F2MeleeWeapons/F2MeleeWeaponPack.fmp'
    if f2_fmp.is_file():
        run('fmp_refusal_ui', [sys.executable, repo/'tools/test_mod_action_error_ui.py', '--gui',
            package/'FableForge.exe', '--install', args.root.resolve(), '--mode', 'fmp', '--fmp', f2_fmp])
    else: skipped.append('fmp_refusal_ui: F2 corpus unavailable')
    for reason in skipped: print('SKIP:', reason, flush=True)
    run('unicode_paths', [sys.executable, repo / 'tools/test_unicode_paths.py', '--gui', package / 'FableForge.exe',
        '--install', args.root.resolve()])
    run('unicode_images', [sys.executable, repo / 'tools/test_texture_unicode_ui.py', '--gui', package / 'FableForge.exe',
        '--install', args.root.resolve()])
    run('unicode_compiler', [sys.executable, repo / 'tools/test_egocore_unicode_paths.py', '--exe', package / 'forge-tools.exe',
        '--install', args.root.resolve()])
    for label, script in (('field_details', 'test_mod_field_details.py'),
                          ('field_choices', 'test_mod_field_choices.py'),
                          ('file_content', 'test_mod_file_content.py'),
                          ('long_values', 'test_mod_long_values.py'),
                          ('conflict_search', 'test_mod_conflict_search.py'),
                          ('unavailable_choices', 'test_mod_unavailable_choice.py'),
                          ('unreported_choices', 'test_mod_unreported_choices.py'),
                          ('setup_asset_restore', 'test_setup_asset_restore.py')):
        run(label, [sys.executable, repo / 'tools' / script, '--gui', package / 'FableForge.exe',
            '--install', args.root.resolve(), '--cwd', cwd])
    for label, script, options in (
            ('setup_locked', 'test_setup_restore_failure_ui.py', []),
            ('setup_partial', 'test_setup_restore_failure_ui.py', ['--partial']),
            ('setup_ambiguity', 'test_setup_restore_ambiguity_ui.py', []),
            ('setup_stage', 'test_setup_stage_restore_ui.py', []),
            ('setup_orphan', 'test_setup_stage_restore_ui.py', ['--orphan']),
            ('setup_confirmation', 'test_setup_confirmation_ui.py', []),
            ('setup_missing_bank', 'test_setup_missing_bank_ui.py', []),
            ('setup_missing_loose', 'test_setup_missing_bank_ui.py', ['--missing-levels']),
            ('setup_missing_wad', 'test_setup_missing_bank_ui.py',
             ['--wad', args.root.resolve() / 'data/Levels/FinalAlbion.wad'])):
        run(label, [sys.executable, repo / 'tools' / script, '--gui', package / 'FableForge.exe',
            '--cwd', cwd, *options])
    for label, options in [('mod_asset_refresh', []), ('mod_asset_separate_root', ['--separate-root']),
                           ('mod_asset_failed', ['--failed-redeploy']),
                           ('mod_asset_failed_separate', ['--failed-redeploy', '--separate-root'])]:
        run(label, [sys.executable, repo / 'tools/test_mod_asset_refresh_ui.py', '--gui',
            package / 'FableForge.exe', '--cwd', cwd, '--install', args.root.resolve(), *options])
    run('texture_destination', [sys.executable, repo / 'tools/test_texture_destination_ui.py',
        '--gui', package / 'FableForge.exe', '--cwd', cwd, '--install', args.root.resolve()])
    for label, options in [('texture_replace_locked', []), ('texture_replace_invalid', ['--invalid']),
                           ('texture_add_locked', ['--add']), ('texture_add_invalid', ['--add', '--invalid'])]:
        run(label, [sys.executable, repo / 'tools/test_texture_replace_failure_ui.py',
            '--gui', package / 'FableForge.exe', '--install', args.root.resolve(), *options])
    run('texture_export_failure', [sys.executable, repo / 'tools/test_texture_export_failure_ui.py',
        '--gui', package / 'FableForge.exe', '--cwd', cwd, '--install', args.root.resolve()])
    assert pending_start is None, f'workflow not found or unavailable: {pending_start}'
    assert not pending_only, f'workflows not found or unavailable: {sorted(pending_only)}'
    (work / 'result.json').write_text(json.dumps({'archive': str(archive), 'sha256': archive_hash,
        'package': str(package), 'checks': checks, 'not_run': not_run,
        'skipped': skipped, 'passed': True}, indent=2), encoding='utf-8')
    print('Extracted package workflows: PASS', len(checks), 'run,', len(not_run), 'not run,', len(skipped), 'skipped')


if __name__ == '__main__':
    main()
