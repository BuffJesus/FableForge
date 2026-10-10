"""New 2D textures must not inherit a volume or animation donor's archive type."""
from collections import Counter
from pathlib import Path
import json
import struct
import subprocess
import tempfile
from PIL import Image
from test_big_entry_markers import archive


def records(path):
    data=path.read_bytes();word=lambda at:struct.unpack_from('<I',data,at)[0]
    directory=word(8);fields=data.index(0,directory+4)+1
    count=word(fields+4);at=word(fields+8)
    types=dict(struct.unpack_from('<II',data,at+4+i*8) for i in range(word(at)))
    at+=4+8*word(at);rows={}
    for _ in range(count):
        marker,identity,kind,length,offset,dev,size=struct.unpack_from('<7I',data,at);at+=28
        name=data[at:at+size].decode('ascii');at+=size+4
        sources=word(at);at+=4
        for _ in range(sources):size=word(at);at+=4+size
        size=word(at);at+=4;info=data[at:at+size];at+=size
        rows[identity]={'marker':marker,'type':kind,'name':name,'info':info,'payload':data[offset:offset+length]}
    assert types==dict(Counter(row['type'] for row in rows.values())),types
    return rows


def main():
    repo=Path(__file__).resolve().parents[1]
    work=Path(tempfile.mkdtemp(prefix='texture-entry-types-',dir=repo/'build'))
    print('Evidence retained at',work,flush=True)
    image=work/'new.png';Image.new('RGBA',(2,2),(0,255,0,255)).save(image)
    checks=[]
    for kind,depth,frames,frame_width in ((1,0,2,1),(4,2,1,2)):
        source=work/(str(kind)+'-source.big');output=work/(str(kind)+'-out.big')
        payload=bytes([0,0,20,255])*(4*max(depth,1))
        archive(source,b'BIGB',[('GBANK_MAIN_PC',122,[(7,kind,'Donor',payload)])])
        raw=bytearray(source.read_bytes());word=lambda at:struct.unpack_from('<I',raw,at)[0]
        directory=word(8);fields=raw.index(0,directory+4)+1
        record=word(fields+8);record+=4+8*word(record)
        at=record+28+word(record+24)+8;assert word(at)==34;at+=4
        info=bytearray(34);struct.pack_into('<6H',info,0,2,2,depth,frame_width,2,frames)
        struct.pack_into('<I',info,12,1);info[17]=1;struct.pack_into('<I',info,20,len(payload))
        info[28:]=bytes([1,32,8,8,8,8]);raw[at:at+34]=info;source.write_bytes(raw)
        original=records(source)[7]
        verified=subprocess.run([str(repo/'build/forge-tools.exe'),'texture','verify',str(source),'Donor'],capture_output=True,text=True,timeout=30)
        assert verified.returncode==0,(verified.stdout,verified.stderr)
        added=subprocess.run([str(repo/'build/forge-tools.exe'),'texture','import',str(source),str(output),
                              'New',str(image),'--add'],capture_output=True,text=True,timeout=30)
        (work/(str(kind)+'.log')).write_text(added.stdout+added.stderr,encoding='utf-8')
        assert added.returncode==0,(added.stdout,added.stderr)
        result=records(output)
        assert result[7]==original and source.read_bytes()==raw
        assert result[8]['type']==0,(kind,'new static texture inherited archive type',result[8]['type'])
        assert result[8]['marker']==42 and result[8]['name']=='New'
        assert struct.unpack_from('<I',result[8]['info'],12)[0]==31 # addition still defaults to DXT1
        checks.append(kind)
    (work/'result.json').write_text(json.dumps({'passed':True,'donor_types':checks},indent=2)+'\n',encoding='utf-8')
    print(len(checks),'texture entry-type checks passed')


if __name__=='__main__':
    main()
