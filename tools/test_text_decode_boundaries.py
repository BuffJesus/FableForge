"""Text show refuses truncated strings/tags without modifying the archive."""
from pathlib import Path
import json
import struct
import subprocess
import tempfile
from test_big_entry_markers import archive


def main():
    tool=Path(__file__).resolve().parents[1]/'build/forge-tools.exe'
    u32=lambda n:struct.pack('<I',n)
    payload='Line'.encode('utf-16le')+bytes(2)+u32(0)*3+u32(1)+u32(0)+b'ANIM:TEST\0'
    checks=0
    with tempfile.TemporaryDirectory(prefix='forge-text-decode-') as directory:
        path=Path(directory)/'text.big'
        cases=[(payload,0,''),(payload[:-1],1,'unterminated tag name'),
               (payload[:-len(b'ANIM:TEST\0')],1,'unterminated tag name'),
               (b'A\0',1,'unterminated UTF-16 content')]
        for data,code,reason in cases:
            archive(path,b'BIGB',[('TEXT_ENGLISH_MAIN',106,[(1,0,'TXT_TEST',data)])])
            original=path.read_bytes()
            run=subprocess.run([str(tool),'text','show',str(path),'TXT_TEST','--json'],
                               capture_output=True,text=True,timeout=30)
            assert run.returncode==code,(run.stdout,run.stderr)
            if reason: assert reason in run.stderr,run.stderr
            else: assert json.loads(run.stdout)['tags']==[{'name':'ANIM:TEST','position':0}]
            assert path.read_bytes()==original
            checks+=1
    print('Text decode boundaries:',checks,'checks passed; source bytes unchanged.')


if __name__=='__main__':
    main()
