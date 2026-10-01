"""Hidden overview population comparison and void-ray regression checks."""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',type=Path,default=ROOT/'build/FableForge.exe')
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    out=args.output.resolve(); out.mkdir(parents=True,exist_ok=True)
    report={}; images=[]; boxes=[]
    for limit in (4,32):
        label=str(limit)
        lines=['wait_maps','wait_ready','set world_detail 0','set world_aa 1',
               'set preview_water 0',f'set world_overview_batch_limit {limit}',
               'world_tab 1','wait_world_tiles','world_camera 927 4220 0 0.8 0.35 200',
               'world_pose 927 4220 150 0.8 0.35',
               'set world_3d 1','frames 1','assert_state world_overview_first_map HookCoast','wait_world_tiles',
               'world_pose 2700 4400 7000 0.8 1.4','mouse_move 20 20','clear_toasts',
               'frames 3','dump_state',f'screenshot {(out/f"{label}.png").as_posix()}',
               'assert_world_ray -100 4220 -1000 1 0 0 -',
               'assert_world_ray 850 4220 10000 0 0 1 -',
               'assert_world_ray 850 4220 10000 0 0 -1 *',
               # Hover the empty underside rather than intersecting a tile side.
               'world_pose -100 4220 -1000 -1.5707963 0',
               'mouse_move viewport','frames 3','assert_state world_hover',
               'quit']
        script=out/f'{label}.txt';script.write_text('\n'.join(lines)+'\n')
        subprocess.run([sys.executable,str(ROOT/'tools/test_world_streaming.py'),'--exe',str(args.exe.resolve()),
                        '--runs','1','--script',str(script),'--frame-ms','33','--size','1280x800'],cwd=ROOT,check=True)
        log=Path(str(script)+'.log').read_text()
        keys=['world_overview_upload_frames','world_thumbnail_upload_frames','texture_pool_gpu_bytes',
              'viewport_x','viewport_y','viewport_width','viewport_height']
        state={key:float(re.findall(r'\b'+key+r'=([\d.]+)',log)[0]) for key in keys}
        report[label]=state
        x,y,w,h=[state[key] for key in keys[-4:]]
        box=(int(x)+2,int(y)+35,int(x+w)-2,int(y+h)-2); boxes.append(box)
        with Image.open(out/f'{label}.png') as im: images.append(np.asarray(im.convert('RGB').crop(box)))
    (out/'report.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
    if boxes[0]!=boxes[1] or not np.array_equal(*images): raise RuntimeError('Upload pacing changed settled overview pixels')
    if report['4']['texture_pool_gpu_bytes']!=report['32']['texture_pool_gpu_bytes']: raise RuntimeError('Texture payload changed')
    # A slow adapter may hit the time budget before the count limit. Report its
    # result without demanding a hardware-independent frame-count improvement.


if __name__=='__main__': main()
