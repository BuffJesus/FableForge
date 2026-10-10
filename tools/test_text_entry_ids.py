"""Text insertion IDs: explicit holes, strict decimal input and refusal preservation."""
from pathlib import Path
import subprocess
import tempfile

from test_big_entry_markers import archive, markers
from test_fmp_links import package_records


def main():
    repo = Path(__file__).resolve().parents[1]
    tool = repo/'build/forge-tools.exe'
    checks = 0
    with tempfile.TemporaryDirectory(prefix='forge-text-ids-') as directory:
        root = Path(directory)
        source, output = root/'source.big', root/'output.big'
        payload = 'Original'.encode('utf-16le') + bytes(18)
        archive(source,b'BIGB',[('TEXT_ENGLISH_MAIN',106,[(0xffffffff,0,'donor',payload)])])
        original = source.read_bytes()

        def run(arguments, accepted):
            nonlocal checks
            result = subprocess.run([str(tool),'text','set',str(source),'new','New line',
                                     '--out',str(output),*arguments],capture_output=True,timeout=30)
            assert (result.returncode==0)==accepted, (arguments,result.stdout,result.stderr)
            assert source.read_bytes()==original
            checks += 1
            return result

        run(['--id','7'],True)
        assert markers(output)=={0xffffffff:0,7:42}
        records=package_records(output,by_id=True)['TEXT_ENGLISH_MAIN']
        assert records[0xffffffff][0]==payload
        assert records[7][0].startswith('New line'.encode('utf-16le')+bytes(2))
        previous=output.read_bytes()
        for value in ('7junk','7.0',' 7','7 ','+7','-1','4294967296','18446744073709551616','0','4294967295',''):
            run(['--id',value],False)
            assert output.read_bytes()==previous
        run([],False)
        assert output.read_bytes()==previous
    print(f'Text entry IDs: {checks} checks passed; input and prior output preserved on refusal.')


if __name__=='__main__':
    main()
