#!/usr/bin/env python3
"""Check joint authoring saves, reopening and game-bank isolation on owned copies."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(os.environ.get('FABLE_ROOT',
        r'C:/Programs/Steam/steamapps/common/Fable The Lost Chapters')))
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='terrain-drafts-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('Evidence retained at', work, flush=True)
    root = work / 'install'
    files = ['data/Levels/' + name for name in ('FinalAlbion.wld', 'FinalAlbion.bwd',
        'FinalAlbion.wad', 'FinalAlbion_RT.stb', 'FinalAlbion.gtg')]
    files += ['data/CompiledDefs/game.bin', 'data/CompiledDefs/names.bin', 'data/graphics/pc/textures.big']
    for name in files:
        dest = root / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(args.root / name, dest)
    env = dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1')
    def run(name, lines, size='1440x900'):
        script = work / (name + '.txt')
        script.write_text('\n'.join(lines) + '\n', encoding='utf-8')
        result = subprocess.run([str(repo / 'build/FableForge.exe'), '--install', str(root),
            '--size', size, '--auto', str(script)], cwd=repo, env=env,
            capture_output=True, text=True, timeout=240)
        (work / (name + '.output.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        log = Path(str(script) + '.log').read_text(encoding='utf-8')
        assert result.returncode == 0 and 'RESULT PASS' in log, log[-6000:] + result.stderr
    def hashes():
        result = {}
        for name in files:
            with (root / name).open('rb') as stream:
                result[name] = hashlib.file_digest(stream, 'sha256').hexdigest()
        return result
    def key(name):
        return ['key_down ' + name, 'frames 3', 'key_up ' + name, 'frames 3']
    base = ['wait_maps', 'wait_ready', 'select TeleporterGreatwood', 'wait_loaded',
            'wait_foliage', 'edit 1', 'tour -1']
    level = root / 'data/Levels/FinalAlbion/TeleporterGreatwood'
    draft = Path(str(level) + '.lev.forge-draft')
    for loose in (False, True):
        if loose:
            wad = root / 'data/Levels/FinalAlbion.wad'
            subprocess.run([str(repo / 'build/forge-tools.exe'), 'wad', 'extract', str(wad), str(root)],
                cwd=repo, check=True, capture_output=True, timeout=180)
            renamed = wad.with_name('_FinalAlbion.wad')
            assert wad.resolve().is_relative_to(work) and renamed.resolve().is_relative_to(work)
            wad.rename(renamed)
            files.remove('data/Levels/FinalAlbion.wad')
            files.append('data/Levels/_FinalAlbion.wad')
            files += [p.relative_to(root).as_posix() for p in (root / 'data/Levels/FinalAlbion').rglob('*.lev')]
        baseline = hashes()
        layout = 'loose' if loose else 'stock'
        for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
            label = layout + '-' + size
            lines = base + [f'set uiscale {scale}', 'terrain_mode 17', 'brush 2 0',
                'set shore_set 0', 'terrain_stroke 30 30 0', 'snapshot_shore',
                'set shore_set 1', 'brush .25 0', 'terrain_stroke 30 30 0',
                'assert_shore_changes 1', 'assert_state doc_dirty 0',
                'assert_state terrain_draft_dirty 1', 'reveal btn_save', 'frames 3',
                'reveal btn_save', 'frames 3', 'click btn_save', 'frames 3',
                'assert_state terrain_draft_dirty 0', 'assert_state terrain_dirty 1',
                'reveal btn_terrain_deploy', 'frames 3', 'assert_widget btn_terrain_deploy',
                'set unsaved_prompt 1', 'select Greatwood_1', 'wait_loaded',
                'select TeleporterGreatwood', 'wait_loaded', 'wait_foliage',
                'assert_state terrain_draft_dirty 0', 'assert_state terrain_dirty 1',
                'assert_shore_changes 1', 'terrain_set_all 20', 'wait_foliage',
                'place OBJECT_BARREL_BREAKABLE DraftProbe']
            lines += ['key_down Ctrl'] + key('S') + ['key_up Ctrl', 'frames 3',
                'assert_state doc_dirty 0', 'assert_state terrain_draft_dirty 0',
                'assert_state terrain_dirty 1', 'select Greatwood_1', 'wait_loaded',
                'select TeleporterGreatwood', 'wait_loaded', 'wait_foliage',
                'select_script DraftProbe', 'assert_height 30 30 20',
                'terrain_set_all 21', 'wait_foliage']
            lines += key('F6') + ['assert_state terrain_draft_dirty 0', 'assert_height 30 30 21',
                'terrain_set_all 22', 'wait_foliage']
            lines += key('F7') + ['wait_loaded', 'wait_foliage', 'assert_state terrain_draft_dirty 0',
                'assert_height 30 30 22', 'select_script DraftProbe',
                'terrain_set_all 23', 'wait_foliage', 'select Greatwood_1', 'frames 3',
                'assert_widget btn_unsaved_save', 'click btn_unsaved_save', 'wait_loaded',
                'select TeleporterGreatwood', 'wait_loaded', 'wait_foliage',
                'assert_state terrain_draft_dirty 0', 'assert_height 30 30 23',
                'select_script DraftProbe', 'dump_log', 'quit']
            run(label, lines, size)
            assert draft.is_file(), 'missing terrain authoring draft'
            assert 'DraftProbe' in Path(str(level) + '.tng').read_text(encoding='utf-8')
            assert baseline == hashes(), 'authoring save changed an active terrain bank'
            # A new process must restore the draft, not only a map switch in the old process.
            run(label + '-reopen', base + ['assert_height 30 30 23', 'assert_state terrain_dirty 1',
                'assert_state terrain_draft_dirty 0', 'select_script DraftProbe', 'quit'], size)
            result = subprocess.run([str(repo / 'build/forge.exe'), 'restore', '--install', str(root), '--forget'],
                cwd=repo, capture_output=True, text=True, timeout=60)
            (work / (label + '-restore.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
            assert result.returncode == 0 and not draft.exists(), result.stdout + result.stderr
            assert baseline == hashes(), 'Restore changed an original terrain bank'
            assert not list(root.rglob('*.forge-created')), 'Restore left draft creation markers'
            print(label, 'button, Ctrl+S, F6/F7, prompt, reopen, isolated banks and Restore: PASS', flush=True)
        # Reopening a saved draft must bake against the original game heights.
        # Compare every pack byte to a direct write of the same edit (including foliage).
        reference = work / (layout + '-reference-pack')
        reopened = work / (layout + '-reopened-pack')
        edit = ['terrain_set_all 24', 'wait_foliage']
        finish = ['deploy_terrain', 'wait_file_job', 'wait_terrain', 'quit']
        run(layout + '-reference', base + edit + [f'pack_dest {reference.as_posix()}',
            'deploy_terrain', 'wait_file_job', 'wait_terrain', 'assert_state terrain_dirty 0',
            'assert_state terrain_draft_dirty 1', 'quit'])
        run(layout + '-save-for-bake', base + edit + ['save_draft', 'quit'])
        run(layout + '-bake-reopened', base + ['assert_height 30 30 24',
            f'pack_dest {reopened.as_posix()}'] + finish)
        def pack_bytes(path):
            return {p.relative_to(path).as_posix(): p.read_bytes() for p in path.rglob('*') if p.is_file()}
        expected = pack_bytes(reference)
        assert len(expected) == 3 and expected == pack_bytes(reopened), 'draft reopen changed baked pack bytes'
        run(layout + '-write-game', base + ['assert_height 30 30 24', 'deploy_terrain',
            'wait_file_job', 'wait_terrain', 'assert_state terrain_dirty 0',
            'assert_state terrain_draft_dirty 0', 'quit'])
        assert not draft.exists(), 'game write did not retire the terrain draft'
        result = subprocess.run([str(repo / 'build/forge.exe'), 'restore', '--install', str(root), '--forget'],
            cwd=repo, capture_output=True, text=True, timeout=60)
        (work / (layout + '-write-restore.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        assert result.returncode == 0 and baseline == hashes(), 'game-write Restore changed original banks'
        assert not list(root.rglob('*.forge-created')), 'retired draft creation marker survived Restore'
        print(layout, 'reopened bake matches direct pack bytes; game write retires draft; exact Restore: PASS', flush=True)



if __name__ == '__main__':
    main()
