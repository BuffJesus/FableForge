#!/usr/bin/env python3
"""An open texture browser follows save-folder changes and fallback without writes."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from test_install_switch_ui import make_install
from PIL import Image


def digest(path):
    with path.open('rb') as stream: return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--gui', type=Path, default=repo/'build/FableForge.exe')
    parser.add_argument('--cwd', type=Path, default=repo)
    args = parser.parse_args()
    gui, cwd = args.gui.resolve(), args.cwd.resolve()
    work = Path(tempfile.mkdtemp(prefix='texture-destination-', dir=repo/'build')).resolve()
    print('Evidence retained at', work, flush=True)
    source = args.install/'data/graphics/pc/textures.big'
    sources = [source, args.install/'data/CompiledDefs/game.bin', args.install/'data/CompiledDefs/names.bin']
    source_hashes = {p: digest(p) for p in sources}
    first, second, empty = work/'selected-install', work/'other-save-folder', work/'empty-save-folder'
    for root in [first, second]:
        make_install(root, 'TextureDestination', 4)
        for bank in ['game.bin', 'names.bin']:
            shutil.copyfile(args.install/'data/CompiledDefs'/bank, root/'data/CompiledDefs'/bank)
        target = root/'data/graphics/pc/textures.big'
        target.parent.mkdir(parents=True)
        shutil.copyfile(source, target)
    empty.mkdir()
    image = work/'marker.png'
    Image.new('RGB', (64, 64), (255, 0, 0)).save(image)
    blue = work/'blue.png'
    Image.new('RGB', (64, 64), (0, 0, 255)).save(blue)
    shared = subprocess.run([str(gui.with_name('forge.exe')), 'texture-add', 'ROOT_SHARED_TEST',
        str(image), '--install', str(first)], cwd=cwd, capture_output=True, text=True, timeout=90)
    (work/'shared.log').write_text(shared.stdout+shared.stderr, encoding='utf-8')
    assert shared.returncode == 0, shared.stdout+shared.stderr
    shutil.copyfile(first/'data/graphics/pc/textures.big', second/'data/graphics/pc/textures.big')
    replaced = subprocess.run([str(gui.with_name('forge.exe')), 'texture-replace', 'ROOT_SHARED_TEST',
        str(blue), '--install', str(second)], cwd=cwd, capture_output=True, text=True, timeout=90)
    (work/'replace.log').write_text(replaced.stdout+replaced.stderr, encoding='utf-8')
    assert replaced.returncode == 0, replaced.stdout+replaced.stderr
    result = subprocess.run([str(gui.with_name('forge.exe')), 'texture-add', 'ROOT_SWITCH_TEST',
        str(blue), '--install', str(second)], cwd=cwd, capture_output=True, text=True, timeout=90)
    (work/'add.log').write_text(result.stdout+result.stderr, encoding='utf-8')
    assert result.returncode == 0, result.stdout+result.stderr
    before = {p: digest(p) for root in [first, second, empty] for p in root.rglob('*') if p.is_file()}
    for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
        case = work/size
        case.mkdir()
        for label in ['selected', 'other', 'fallback']: (case/label).mkdir()
        def export(label):
            return ['reveal img_texture', 'frames 3', 'reveal img_texture', 'frames 3',
                'dump_widget img_texture', 'clear_toasts', f'screenshot {(case/label/"preview.png").as_posix()}',
                f'set outdir {(case/label).as_posix()}', 'reveal btn_tex_export', 'frames 3',
                'reveal btn_tex_export', 'frames 3', 'click btn_tex_export', 'frames 3']
        script = case/'destination.txt'
        lines = ['wait_maps', 'wait_ready', f'set uiscale {scale}', 'assets_tab 0', 'frames 4',
            'texture_select ROOT_SHARED_TEST', 'frames 3']+export('selected')+[
            'dump_state', f'set saveroot {second.as_posix()}', 'frames 4',
            'texture_select ROOT_SHARED_TEST', 'frames 3']+export('other')+[
            'texture_select ROOT_SWITCH_TEST', 'frames 3', 'assert_state texture_selected ROOT_SWITCH_TEST',
            'reveal img_texture', 'frames 3', 'assert_widget img_texture', 'dump_state',
            'clear_toasts', f'screenshot {(case/"other.png").as_posix()}',
            f'set saveroot {second.as_posix()}', 'frames 3',
            'assert_state texture_selected ROOT_SWITCH_TEST', f'set saveroot {empty.as_posix()}',
            'frames 4', 'assert_state texture_selected -', 'dump_state',
            'texture_select ROOT_SHARED_TEST', 'frames 3']+export('fallback')+[
            'set saveroot', 'frames 3',
            'assert_state texture_selected ROOT_SHARED_TEST',
            f'set saveroot {second.as_posix()}', 'frames 4', 'texture_select ROOT_SWITCH_TEST',
            'frames 3', 'assert_state texture_selected ROOT_SWITCH_TEST', 'set saveroot',
            'frames 4', 'assert_state texture_selected -', 'dump_state', 'quit']
        script.write_text('\n'.join(lines)+'\n', encoding='utf-8')
        result = subprocess.run([str(gui), '--install', str(first), '--size', size,
            '--auto', str(script)], cwd=cwd, capture_output=True, text=True, timeout=90,
            env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1',
                APPDATA=str(case/'appdata'), LOCALAPPDATA=str(case/'localappdata')))
        (case/'output.log').write_text(result.stdout+result.stderr, encoding='utf-8')
        log = Path(str(script)+'.log').read_text(encoding='utf-8')
        assert {p: digest(p) for root in [first, second, empty] for p in root.rglob('*') if p.is_file()} == before
        assert result.returncode == 0 and 'RESULT PASS' in log, '\n'.join(log.splitlines()[-15:])
        counts = list(map(int, re.findall(r'\btextures_count=(\d+)', log)))
        assert len(counts) == 4 and counts[1] == counts[0]+1 and counts[2:] == [counts[0]]*2, counts
        for label, channel in [('selected', 0), ('other', 2), ('fallback', 0)]:
            with Image.open(case/label/'ROOT_SHARED_TEST.png') as png:
                assert png.size == (64, 64)
                colours = set(png.convert('RGB').getdata())
                assert len(colours) == 1 and all(pixel[channel] > 240 and
                    all(pixel[i] < 10 for i in range(3) if i != channel) for pixel in colours), (label, colours)
        bounds = re.findall(r'widget_rect img_texture ([\d.-]+) ([\d.-]+) ([\d.-]+) ([\d.-]+)', log)
        assert len(bounds) == 3
        for (label, channel), rect in zip([('selected', 0), ('other', 2), ('fallback', 0)], bounds):
            left, top, right, bottom = map(float, rect)
            with Image.open(case/label/'preview.png') as preview:
                pixel = preview.convert('RGB').getpixel((int((left+right)/2), int(top+10)))
                assert pixel[channel] > 240 and all(pixel[i] < 10 for i in range(3) if i != channel), (label, pixel, rect)
        print(size, 'bank changes refresh rows/preview, same root preserves selection, fallback clears it: PASS', flush=True)
    assert all(digest(p) == value for p, value in source_hashes.items())
    print('Source banks and all GUI target inventories unchanged: PASS', flush=True)


if __name__ == '__main__':
    main()
