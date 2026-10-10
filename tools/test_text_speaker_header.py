#!/usr/bin/env python3
"""CLI text writers synchronize narrator indices and preserve files on refusal."""
import argparse,json,struct,subprocess,tempfile
from pathlib import Path
from test_mod_asset_choice_contributors import bank
from test_text_content_edit_ui import metadata
from test_fmp_links import package_records

def main():
    repo=Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--tool',type=Path,default=repo/'build/forge-tools.exe')
    ap.add_argument('--cwd',type=Path,default=repo)
    ap.add_argument('--work-root',type=Path,default=repo/'build')
    a=ap.parse_args();work=Path(tempfile.mkdtemp(prefix='text-speaker-header-',dir=a.work_root.resolve()))
    print('Evidence retained at',work,flush=True);checks=0
    def payload(content,speaker='NONE',identifier=''):
        field=lambda s:struct.pack('<I',len(s))+s.encode('ascii')
        return content.encode('utf-16-le')+bytes(2)+field('Speech.lug')+field(speaker)+field(identifier)+bytes(4)
    def records(names=('NONE','HERO','','FARMER'),header=bytes(4)):
        return {'TXT_A':{'id':10,'type':0,'header':header,'payload':payload('Original')},
            'TXT_OTHER':{'id':11,'type':0,'header':bytes(4),'payload':payload('Other')},
            '[NarratorList]':{'id':12,'type':2,'header':struct.pack('<I',len(names))+b''.join(n.encode('ascii')+b'\0' for n in names),'payload':b''}}
    def run(command,accepted,label):
        nonlocal checks
        r=subprocess.run([str(a.tool.resolve()),'text',*command],cwd=a.cwd.resolve(),capture_output=True,text=True,encoding='utf-8',timeout=30)
        (work/(label+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8')
        assert (r.returncode==0)==accepted,r.stdout+r.stderr;checks+=1
    source=work/'source.big';output=work/'output.big';manifest=work/'import.json'
    bank(source,'TEXT_MAIN',records());original=source.read_bytes();before=metadata(source)
    run(['set',str(source),'TXT_A','Changed','--speaker','HERO','--speech-bank','Speech.lug','--out',str(output)],True,'set')
    after=metadata(output);key=(b'TEXT_MAIN',10)
    assert after[key][:-1]==before[key][:-1] and after[key][-1][:-4]==before[key][-1][:-4]
    assert struct.unpack('<I',after[key][-1][-4:])[0]==1
    for k in before:
        if k!=key:assert after[k]==before[k]
    assert package_records(output,by_id=True)['TEXT_MAIN'][11][0]==records()['TXT_OTHER']['payload']
    manifest.write_text(json.dumps({'entries':[{'name':'TXT_NEW','content':'Added','speaker':'FARMER','speech_bank':'Speech.lug'}]}))
    run(['import',str(source),str(manifest),'--out',str(output)],True,'import-add')
    after=metadata(output);assert struct.unpack('<I',after[(b'TEXT_MAIN',13)][-1][-4:])[0]==3
    for k in before:assert after[k]==before[k]
    run(['set',str(source),'TXT_A','First inplace','--speaker','HERO','--in-place'],True,'inplace-first')
    backup=Path(str(source)+'.forge-orig');assert backup.read_bytes()==original
    assert struct.unpack('<I',metadata(source)[key][-1][-4:])[0]==1
    run(['set',str(source),'TXT_A','Empty speaker','--speaker','','--in-place'],True,'empty-speaker')
    assert struct.unpack('<I',metadata(source)[key][-1][-4:])[0]==2 and backup.read_bytes()==original
    backup.unlink() # Own backup from the preceding two checks.
    previous=output.read_bytes()
    for label,fixture in [('missing-list',{k:v for k,v in records().items() if k!='[NarratorList]'}),
        ('bad-index',records(header=struct.pack('<I',99))),('bad-size',records(header=b'xx')),
        ('missing-header',records(header=b'')),
        ('narrator-payload',dict(records(),**{'[NarratorList]':{'id':12,'type':2,'header':struct.pack('<I',1)+b'NONE\0','payload':b'bad'}})),
        ('ambiguous-name',records(names=('NONE','HERO','HERO'))),('unknown-name',records(names=('NONE',))),
        ('bad-list',dict(records(),**{'[NarratorList]':{'id':12,'type':2,'header':b'bad','payload':b''}}))]:
        bank(source,'TEXT_MAIN',fixture);baseline=source.read_bytes()
        run(['set',str(source),'TXT_A','Refused','--speaker','HERO','--out',str(output)],False,label+'-set')
        manifest.write_text(json.dumps({'entries':[{'name':'FIRST','content':'First','speaker':'NONE'},
            {'name':'TXT_A','content':'Refused','speaker':'HERO'}]}))
        run(['import',str(source),str(manifest),'--in-place'],False,label+'-import')
        assert source.read_bytes()==baseline and output.read_bytes()==previous
        assert not Path(str(source)+'.forge-orig').exists() and not list(work.glob('.forge-text-write-*'))
    print(checks,'speaker header synchronization/addition/refusal cases: PASS',flush=True)

if __name__=='__main__':main()
