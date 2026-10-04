#!/usr/bin/env python3
"""Audit every extracted/archive FMP on retail and complete-overhaul definitions.

Accepted builds are checked for preservation; unsupported packages are recorded
as refusals, never counted as compatible mods. All outputs remain in scratch.
"""
import argparse, hashlib, json, shutil, subprocess, tempfile
from pathlib import Path
from test_fmp_links import package_records, read_bank
from test_mod_bank_refusal import snapshot


def digest(path):
    with path.open('rb') as stream: return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    repo = Path(__file__).resolve().parents[1]
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--install', type=Path, required=True)
    p.add_argument('--corpus', type=Path, required=True)
    args = p.parse_args()
    work = Path(tempfile.mkdtemp(prefix='fmp-corpus-audit-', dir=repo/'build'))
    print('Evidence retained at', work, flush=True)
    originals = sorted(args.corpus.rglob('*.fmp'))
    archive_hashes = {}
    for archive in sorted(args.corpus.iterdir()):
        if archive.suffix.lower() not in ('.rar', '.7z', '.zip'): continue
        archive_hashes[str(archive)] = digest(archive)
        listed = subprocess.run(['tar', '-tf', str(archive)], capture_output=True, timeout=60)
        assert listed.returncode == 0, (archive, listed.stderr)
        for member in listed.stdout.decode('utf-8').splitlines():
            if not member.lower().endswith('.fmp'): continue
            data = subprocess.run(['tar', '-xOf', str(archive), member], capture_output=True, timeout=120)
            assert data.returncode == 0, (archive, member, data.stderr)
            destination = work/'archive-fmps'/archive.stem/member
            assert destination.resolve().is_relative_to((work/'archive-fmps').resolve())
            destination.parent.mkdir(parents=True, exist_ok=True); destination.write_bytes(data.stdout)
            originals.append(destination)
    # Extracted and archived copies are the same fixture; retain every origin.
    unique = {}
    for path in originals: unique.setdefault(digest(path), []).append(path)
    print('Unique FMPs:', len(unique), 'from', len(originals), 'available copies', flush=True)
    base, overhaul = work/'baseline', work/'overhaul-definitions'
    for root, source in [(base,args.install), (overhaul,args.corpus/'_peek/AeonEdition')]:
        target = root/'data/CompiledDefs'; target.mkdir(parents=True)
        for bank in ('game.bin','names.bin','script.bin','frontend.bin'):
            shutil.copyfile(source/'data/CompiledDefs'/bank, target/bank)
    baselines = {'retail':read_bank(base), 'complete_overhaul':read_bank(overhaul)}
    before = {str(root):snapshot(root) for root in (base,overhaul)}
    package_hashes = {str(path):digest(path) for path in originals}
    rows = []
    refusals = ('FMP linking:', 'FMP banks:', 'FMP records:', 'FMP record',
                'FMP link', 'unnamed definition', 'untyped', 'definition type')
    tool = repo/'build/forge-tools.exe'
    for index,(sha,paths) in enumerate(unique.items()):
        package = paths[0]
        for mode, intact in baselines.items():
            out = work/'outputs'/f'{index:03d}-{mode}'; out.mkdir(parents=True)
            (out/'prior.txt').write_bytes(b'prior output retained on refusal')
            prior = snapshot(out)
            sources = [str(package)] if mode == 'retail' else [str(overhaul),str(package)]
            argv = [str(tool),'mods','merge',str(base),str(out)]
            if mode != 'retail': argv += ['--install-data']
            argv += ['--with',*sources,'--json']
            proc = subprocess.run(argv,capture_output=True,timeout=180,cwd=repo)
            diagnostic = proc.stderr.decode('utf-8',errors='replace')
            (work/f'{index:03d}-{mode}.log').write_bytes(proc.stdout+proc.stderr)
            row = {'sha256':sha,'origins':list(map(str,paths)), 'mode':mode,'returncode':proc.returncode}
            if proc.returncode:
                assert snapshot(out) == prior, (package,mode,'failed operation changed output')
                row.update(status='refused',diagnostic=diagnostic.strip())
                assert any(text.lower() in diagnostic.lower() for text in refusals), (package,mode,diagnostic)
            else:
                banks = package_records(package)
                imported = set(banks.get('Objects',{})) | set(banks.get('GameBINEntries',{}))
                composed = read_bank(out)
                preserved = 0
                for entry_index,entry in enumerate(intact):
                    if entry['name'] not in imported:
                        assert composed[entry_index] == entry, (package,mode,entry_index,entry['name'])
                        preserved += 1
                row.update(status='accepted_preservation_checked',unaffected_records_preserved=preserved)
            rows.append(row)
            (work/'progress.json').write_text(json.dumps(rows,indent=2))
        print(index+1,'/',len(unique),package.name,':',rows[-2]['status'], '/',rows[-1]['status'],flush=True)
    assert {str(root):snapshot(root) for root in (base,overhaul)} == before
    assert {str(path):digest(path) for path in originals} == package_hashes
    assert {str(Path(path)):digest(Path(path)) for path in archive_hashes} == archive_hashes
    counts = {mode:{status:sum(row['mode']==mode and row['status']==status for row in rows)
        for status in ('accepted_preservation_checked','refused')} for mode in baselines}
    result = {'passed':True,'unique_fmps':len(unique),'counts':counts,'rows':rows,
              'whole_overhaul_assets_tested':False,'gameplay_tested':False}
    (work/'result.json').write_text(json.dumps(result,indent=2))
    print('Audit complete:',json.dumps(counts),flush=True)


if __name__ == '__main__': main()
