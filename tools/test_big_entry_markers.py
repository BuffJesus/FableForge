"""New retail texture/mesh entries use marker 42 without altering donor rows."""
import argparse
from collections import Counter
import json
from pathlib import Path
import struct
import subprocess
import tempfile

from test_mod_field_details import write_bank
from test_mod_bank_refusal import snapshot
from test_fmp_links import package_records


def archive(path, magic, banks):
    words = lambda *v: struct.pack('<'+'I'*len(v), *v)
    data = bytearray(magic+words(100,0,510))
    locations = []
    for name, version, entries in banks:
        rows = []
        for identity, kind, symbol, payload in entries:
            rows.append((identity,kind,symbol,len(data),len(payload)))
            data += payload
        locations.append((name,version,rows))
    directory_rows = []
    for name, version, rows in locations:
        start = len(data)
        types = Counter(row[1] for row in rows)
        data += words(len(types))
        for kind, count in sorted(types.items()):
            data += words(kind,count)
        for identity, kind, symbol, offset, length in rows:
            raw = symbol.encode('ascii')
            info = bytes(34) if name in ('GBANK_MAIN_PC','maintextures') else b''
            data += words(0,identity,kind,length,offset,0,len(raw))+raw+words(0,0,len(info))+info
        directory_rows.append((name,version,len(rows),start,len(data)-start))
    directory = len(data)
    data += words(len(directory_rows))
    for name, version, count, start, size in directory_rows:
        data += name.encode('ascii')+b'\0'+words(version,count,start,size,1)
    struct.pack_into('<I',data,8,directory)
    if magic == b'BIGB':
        struct.pack_into('<I',data,12,len(data)-directory)
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_bytes(data)


def markers(path):
    data = path.read_bytes()
    word = lambda at: struct.unpack_from('<I',data,at)[0]
    directory = word(8)
    assert data[:4] == b'BIGB' and word(12) == len(data)-directory and word(directory) == 1
    fields = data.index(0,directory+4)+1
    count, start = word(fields+4), word(fields+8)
    at = start+4+8*word(start)
    result = {}
    for _ in range(count):
        marker, identity = word(at), word(at+4)
        at += 28+word(at+24)
        sources = word(at+4); at += 8
        for _ in range(sources):
            at += 4+word(at)
        at += 4+word(at)
        result[identity] = marker
    assert at == start+word(fields+12)
    return result


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool',type=Path,default=repo/'build/forge-tools.exe')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='big-entry-markers-',dir=repo/'build')).resolve()
    assert work.parent == (repo/'build').resolve()
    base = work/'base'; write_bank(base,(1,2,3,4))
    cases = [('data/graphics/pc/textures.big','GBANK_MAIN_PC','maintextures',122,0),
             ('data/graphics/graphics.big','MBANK_ALLMESHES','graphics',205,1)]
    for relative, bank, package_bank, version, kind in cases:
        archive(base/relative,b'BIGB',[(bank,version,[(1,kind,'old',b'old payload')])])
    package = work/'new-assets.fmp'
    archive(package,b'B\0\0\0',[(p,0,[(2,k,'new',b'new payload')]) for _,_,p,_,k in cases])
    before = snapshot(base); package_before = package.read_bytes(); checks = []
    print('Evidence retained at',work,flush=True)
    for mode in ('standalone','composed'):
        out = work/mode
        command = ['fmp','apply',base,package,out] if mode == 'standalone' else ['mods','merge',base,out,'--with',package]
        run = subprocess.run([str(args.tool.resolve()),*map(str,command)],capture_output=True,timeout=120)
        (work/(mode+'.log')).write_bytes(run.stdout+run.stderr)
        assert run.returncode == 0, run.stderr
        for relative, bank, _, _, _ in cases:
            assert markers(out/relative) == {1:0,2:42}, (mode,relative,markers(out/relative))
            payloads = package_records(out/relative,by_id=True)[bank]
            assert payloads[1][0] == b'old payload' and payloads[2][0] == b'new payload'
            checks.append({'route':mode,'file':relative,'passed':True})
        assert snapshot(base) == before and package.read_bytes() == package_before
    (work/'checks.json').write_text(json.dumps(checks,indent=2)+'\n',encoding='utf-8')
    print(f'PASS: {len(checks)} retail insertion checks; donor markers and input bytes preserved')


if __name__ == '__main__':
    main()
