"""Create and save a lip-sync pack without leaving Dialogue, using scratch destinations."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install',type=Path,required=True)
    args=parser.parse_args()
    repo=Path(__file__).resolve().parents[1]
    work=Path(tempfile.mkdtemp(prefix='dialogue-pack-create-',dir=repo/'build'))
    print('Evidence retained at',work,flush=True)
    for size,scale in [('1440x900',1),('800x600',1.5)]:
        dest=work/size;dest.mkdir()
        root=dest/'save';root.mkdir()
        script=dest/'create.txt'
        script.write_text('\n'.join(['wait_maps','wait_ready',f'set saveroot {root.as_posix()}',
            'assets_tab 4',f'set uiscale {scale}','frames 3','reveal header_dialogue_lookup',
            'click header_dialogue_lookup','frames 3','dialogue_select 0 2',
            'reveal button_dialogue_load','click button_dialogue_load','frames 3',
            'reveal button_dialogue_edit','click button_dialogue_edit','frames 3',
            'reveal slider_dialogue_key_0','frames 3','click slider_dialogue_key_0','frames 3',
            'assert_state dialogue_staged 1','reveal input_new_pack','frames 3',
            'reveal input_new_pack','frames 3',
            'click input_new_pack','input_text Dialogue Test','frames 3',
            'reveal btn_new_pack','frames 3','click btn_new_pack','frames 3',
            'assert_state dialogue_staged 1','reveal button_dialogue_add_pack','frames 3',
            'click button_dialogue_add_pack','frames 3','assert_state dialogue_staged 0',
            'assert_state dialogue_pack_added 1',f'screenshot {dest.as_posix()}/saved.png','quit','']),encoding='utf-8')
        result=subprocess.run([str(repo/'build/FableForge.exe'),'--install',str(args.install),
            '--auto',str(script),'--size',size],cwd=repo,capture_output=True,text=True,timeout=120,
            env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1'))
        log=Path(str(script)+'.log').read_text(encoding='utf-8')
        assert result.returncode==0 and 'RESULT PASS' in log,log[-3000:]
        pack=root/'FableForgeMods/Dialogue_Test/forge_pack.json'
        manifest=json.loads(pack.read_text(encoding='utf-8'))
        assert manifest['name']=='Dialogue Test'
        assert len(manifest['lipSync'])==1 and manifest['lipSync'][0]['soundId']==2
        assert manifest['lipSync'][0]['bank']=='LIPSYNC_ENGLISH_MAIN'
        assert (root/'forge_mods.json').is_file()
        assert not (root/'data/lang/English/dialogue.big').exists(),'creation unexpectedly deployed a bank'
        print(size,'inline pack creation, load-order entry and saved lip-sync recipe: PASS',flush=True)


if __name__=='__main__':
    main()
