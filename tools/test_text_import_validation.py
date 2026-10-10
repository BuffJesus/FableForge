"""Unrepresentable text/IDs refuse before output or in-place import writes."""
import json
from pathlib import Path
import subprocess
import tempfile
from test_big_entry_markers import archive, markers


def main():
    tool=Path(__file__).resolve().parents[1]/'build/forge-tools.exe'
    checks=0
    with tempfile.TemporaryDirectory(prefix='forge-text-import-') as directory:
        root=Path(directory)
        source,output,manifest=root/'source.big',root/'output.big',root/'manifest.json'
        archive(source,b'BIGB',[('TEXT_ENGLISH_MAIN',106,[(1,0,'donor','Old'.encode('utf-16le')+bytes(18))])])
        original=source.read_bytes();output.write_bytes(original)
        invalid=[{'name':'new','content':'before\0after'},
                 {'name':'bad\0name','content':'Line'},
                 {'name':'new','content':'Line','speaker':'bad\0speaker'},
                 {'name':'new','content':'Line','speech_bank':'bad\0bank'}]
        invalid += [{'name':'new','content':'Line','id':value}
                    for value in (-1,0,2**32,2**64,1.5,True,'7',None)]
        for row in invalid:
            manifest.write_text(json.dumps({'entries':[{'name':'first','content':'Valid first row'},row]}),encoding='utf-8')
            for destination in (['--out',str(output)],['--in-place']):
                result=subprocess.run([str(tool),'text','import',str(source),str(manifest),*destination],
                                      capture_output=True,timeout=30)
                assert result.returncode!=0,(row,result.stdout,result.stderr)
                assert source.read_bytes()==original and output.read_bytes()==original,(row,destination)
                checks+=1
        manifest.write_text(json.dumps({'entries':[{'name':'new','content':'Caf\u00e9 \U0001f600','id':2**32-1}]}),encoding='utf-8')
        result=subprocess.run([str(tool),'text','import',str(source),str(manifest),'--out',str(output)],
                              capture_output=True,timeout=30)
        assert result.returncode==0,(result.stdout,result.stderr)
        assert markers(output)=={1:0,2**32-1:42} and source.read_bytes()==original
        checks+=1
    print(f'Text import validation: {checks} checks passed; refusals preserve output and in-place source.')


if __name__=='__main__':
    main()
