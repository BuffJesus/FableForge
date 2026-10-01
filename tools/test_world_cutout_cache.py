"""Compare cached/uncached cutout preparation: exact world pixels and GPU payload."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--baseline-exe',type=Path,help='Optional earlier executable for the uncached captures')
    args=parser.parse_args()
    out=args.output.resolve(); out.mkdir(parents=True,exist_ok=True)
    poses=[(3398.75,3612.5,130,.46365,.5),(3550,825,90,.8,.4),
           (850,4220,150,.8,.35),(3398.75,3612.5,130,.46365,.5)]
    keys=['world_cutout_cache_bytes','world_cutout_cache_entries','world_cutout_cache_hits',
          'world_cutout_cache_builds','texture_pool_gpu_bytes',
          'viewport_x','viewport_y','viewport_width','viewport_height']
    report={}; captures={}
    for enabled in [0,1]:
        label='cached' if enabled else 'uncached'
        lines=['wait_maps','wait_ready','set world_detail 0','set world_auto_detail 0',
               'set world_detail_limit 6','set world_aa 1','set preview_water 0',
               f'set world_cutout_cache {enabled}','world_tab 1','wait_world_tiles',
               'set world_3d 1','wait_world_tiles','mouse_move 20 20','clear_toasts']
        for i,pose in enumerate(poses):
            lines+=['set world_detail 0','world_pose '+' '.join(map(str,pose)),
                    'set world_detail 1','wait_world_detail','frames 3','dump_state',
                    f'screenshot {(out/f"{label}-{i}.png").as_posix()}']
        lines+=['quit']
        script=out/f'{label}.txt'; script.write_text('\n'.join(lines)+'\n')
        exe=args.baseline_exe.resolve() if not enabled and args.baseline_exe else ROOT/'build/FableForge.exe'
        subprocess.run([sys.executable,str(ROOT/'tools/test_world_streaming.py'),'--runs','1','--exe',str(exe),
                        '--script',str(script),'--frame-ms','16','--size','1280x800'],cwd=ROOT,check=True)
        log=Path(str(script)+'.log').read_text()
        values={key:[float(v) for v in re.findall(key+r'=([\d.]+)',log)] for key in keys}
        if any(len(v)!=len(poses) for v in values.values()): raise RuntimeError('Missing state captures')
        report[label]=[{key:values[key][i] for key in keys} for i in range(len(poses))]
        captures[label]=[]
        for i,state in enumerate(report[label]):
            x,y,w,h=[state[key] for key in keys[-4:]]
            box=(int(x)+2,int(y)+35,int(x+w)-2,int(y+h)-2)
            captures[label].append((box,np.array(Image.open(out/f'{label}-{i}.png').convert('RGB').crop(box))))
    (out/'report.json').write_text(json.dumps(report,indent=2)); print(json.dumps(report,indent=2))
    for i in range(len(poses)):
        before,after=captures['uncached'][i],captures['cached'][i]
        if before[0]!=after[0] or not np.array_equal(before[1],after[1]):
            raise RuntimeError(f'Cache changed rendered pixels at pose {i}')
        a,b=report['uncached'][i],report['cached'][i]
        if a['texture_pool_gpu_bytes']!=b['texture_pool_gpu_bytes']:
            raise RuntimeError('CPU cache changed GPU texture payload')
        if a['world_cutout_cache_bytes'] or b['world_cutout_cache_bytes']>32*1024*1024:
            raise RuntimeError('Cache residency exceeded its budget')
    if report['cached'][-1]['world_cutout_cache_hits']<=0:
        raise RuntimeError('Fixture did not exercise reuse')
    if report['cached'][-1]['world_cutout_cache_builds']>=report['uncached'][-1]['world_cutout_cache_builds']:
        raise RuntimeError('Cache did not avoid mip preparation')


if __name__=='__main__': main()
