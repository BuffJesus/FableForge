#!/usr/bin/env python3
"""Single-byte metadata exports as valid UTF-8 without touching bank bytes."""
import argparse,csv,json,os,struct,subprocess,tempfile
from pathlib import Path
from test_mod_field_details import write_bank
from test_mod_asset_choice_contributors import bank

def payload(raw):
    content='Content \u03a9 \u6f22\U0001f642'.encode('utf-16-le')+b'\0\0'
    field=struct.pack('<I',len(raw))+raw
    return content+field*3+struct.pack('<Ii',1,-7)+raw+b'\0'

def main():
    repo=Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--gui',type=Path,default=repo/'build/FableForge.exe')
    ap.add_argument('--tool',type=Path,default=repo/'build/forge-tools.exe')
    ap.add_argument('--cwd',type=Path,default=repo)
    ap.add_argument('--work-root',type=Path,default=repo/'build')
    a=ap.parse_args();work=Path(tempfile.mkdtemp(prefix='text-byte-export-',dir=a.work_root.resolve()))
    print('Evidence retained at',work,flush=True)
    for size,scale in [('1440x900',1),('800x600',1.5)]:
        case=work/size;case.mkdir();root=case/'install';write_bank(root,(1,2,3,4))
        levels=root/'data/Levels/FinalAlbion';levels.mkdir(parents=True);(levels/'unused.lev').write_bytes(b'')
        target=root/'data/lang/English/text.big'
        values={'ASCII':b'ASCII', 'BYTE_RANGE':bytes(range(1,256)), 'UTF8_LIKE':b'\xc3\xa9'}
        records={name:{'id':i+1,'type':0,'header':bytes(4),'payload':payload(raw)} for i,(name,raw) in enumerate(values.items())}
        bank(target,'TEXT_MAIN',records);original=target.read_bytes()
        for name,raw in values.items():
            r=subprocess.run([str(a.tool.resolve()),'text','show',str(target),name,'--json'],cwd=a.cwd.resolve(),capture_output=True,timeout=30)
            (case/(name+'.log')).write_bytes(r.stdout+b'\n'+r.stderr)
            assert r.returncode==0,r.stderr
            row=json.loads(r.stdout.decode('utf-8'));expected=raw.decode('latin-1')
            assert row['content']=='Content \u03a9 \u6f22\U0001f642'
            assert [row[k] for k in ('speaker','speech_bank','identifier')]==[expected]*3
            assert row['tags']==[{'position':-7,'name':expected}] and row['metadata_byte_encoding']=='latin1'
        output=case/'all.tsv';script=case/'export.txt'
        script.write_text('\n'.join(['wait_maps','wait_ready',f'set uiscale {scale}','assets_tab 5','wait_texts','frames 3',
            'text_select BYTE_RANGE',f'text_export_path {output.as_posix()}','reveal btn_text_export','frames 3','click btn_text_export',
            'wait_text_export','frames 3','assert_state text_export_succeeded 1','quit'])+'\n',encoding='utf-8')
        env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(case/'profile'),LOCALAPPDATA=str(case/'local'))
        r=subprocess.run([str(a.gui.resolve()),'--install',str(root),'--size',size,'--auto',str(script)],cwd=a.cwd.resolve(),env=env,capture_output=True,timeout=90)
        trace=Path(str(script)+'.log').read_text(encoding='utf-8');assert r.returncode==0 and 'RESULT PASS' in trace,trace[-3500:]
        with output.open(encoding='utf-8-sig',newline='') as f:rows=list(csv.DictReader(f,delimiter='\t'))
        assert len(rows)==3
        for row in rows:
            raw=values[row['name']];expected=raw.decode('latin-1')
            assert [row[k] for k in ('speaker','speech_bank','identifier')]==[expected]*3
            assert json.loads(row['tags'])==[{'position':-7,'name':expected}]
            assert row['content']=='Content \u03a9 \u6f22\U0001f642' and not row['error']
        assert target.read_bytes()==original and not Path(str(target)+'.forge-orig').exists()
        assert not list(case.glob('forge-text-export-*'))
        print(size,'all nonzero 8-bit values, UTF8-like bytes, JSON/TSV and untouched source: PASS',flush=True)
if __name__=='__main__':main()
