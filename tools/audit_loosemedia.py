"""Read-only storage-envelope and decoder evidence for loose Ogg/TGA media.

Requires forge-tools, ffprobe and ffmpeg. Envelope checks and decoding do not certify
all codec packet semantics, native playback/rendering proof, path resolution or source
admission. Every input and decoder executable is hash-pinned and unchanged.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def executable(value, name):
    found = str(value) if value else shutil.which(name)
    if not found or not Path(found).is_file():
        raise RuntimeError(f'{name} executable not found')
    return Path(found).resolve()


def inspect(path, probe, decoder):
    command = [str(probe), '-v', 'error', '-protocol_whitelist', 'file,pipe',
               '-show_entries', 'stream=codec_name,codec_type,channels,sample_rate,width,height,pix_fmt:'
                                'format=format_name,duration,size', '-of', 'json', str(path)]
    run = subprocess.run(command, capture_output=True, text=True, encoding='utf-8', timeout=30)
    if run.returncode:
        raise RuntimeError('probe refused: ' + run.stderr.strip()[:1000])
    metadata = json.loads(run.stdout)
    streams = metadata.get('streams', [])
    expected = 'vorbis' if path.suffix.lower() == '.ogg' else 'targa'
    if len(streams) != 1 or streams[0].get('codec_name') != expected:
        raise RuntimeError(f'expected one {expected} stream')
    if expected == 'vorbis' and metadata.get('format', {}).get('format_name') != 'ogg':
        raise RuntimeError('expected Ogg container')
    command = [str(decoder), '-nostdin', '-v', 'error', '-xerror', '-err_detect', 'explode',
               '-protocol_whitelist', 'file,pipe', '-i', str(path), '-map', '0:0', '-f', 'null', '-']
    run = subprocess.run(command, capture_output=True, text=True, encoding='utf-8', timeout=120)
    if run.returncode:
        raise RuntimeError('decode refused: ' + run.stderr.strip()[:1000])
    return metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, action='append', required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--tool', type=Path, default=Path(__file__).resolve().parents[1]/'build/forge-tools.exe')
    parser.add_argument('--ffprobe', type=Path)
    parser.add_argument('--ffmpeg', type=Path)
    args = parser.parse_args()
    roots = [p.resolve() for p in args.source]
    output = args.out.resolve()
    probe = executable(args.ffprobe, 'ffprobe')
    decoder = executable(args.ffmpeg, 'ffmpeg')
    tool = args.tool.resolve()
    tooling = [Path(__file__).resolve(), tool, probe, decoder]
    if any(output.is_relative_to(root) for root in roots) or output in tooling:
        parser.error('report must not replace source files or tooling')
    before = {str(p): digest(p) for p in tooling}
    report = {'scope': __doc__, 'source_admitted': False, 'complete_storage_review': False,
              'file_limit_bytes': 128 * 1024 * 1024, 'sources': []}
    decoded = refused = storage_refused = 0
    for root in roots:
        files = sorted(p for p in root.rglob('*') if p.is_file() and p.suffix.lower() in ('.ogg', '.tga'))
        if not files:
            raise RuntimeError(f'no loose Ogg/TGA files under {root}')
        rows = []
        for path in files:
            before[str(path)] = digest(path)
            row = {'path': str(path), 'decoder_accepted': False, 'envelope_accepted': False}
            try:
                if path.stat().st_size > report['file_limit_bytes']:
                    raise RuntimeError('media exceeds diagnostic file limit (128 MiB)')
                storage = subprocess.run([str(tool),'world','review-storage',str(path),'--json'],
                                         capture_output=True,text=True,encoding='utf-8',timeout=30)
                row['storage_review'] = json.loads(storage.stdout)
                if storage.returncode not in (0,1) or row['storage_review'].get('storage_supported') != (storage.returncode==0):
                    raise RuntimeError('inconsistent storage review result')
                row['envelope_accepted'] = storage.returncode==0
                row['metadata'] = inspect(path, probe, decoder)
                row['decoder_accepted'] = True
                decoded += 1
            except (RuntimeError, subprocess.TimeoutExpired, ValueError) as error:
                row['reason'] = str(error)
                refused += 1
            storage_refused += not row['envelope_accepted']
            rows.append(row)
        report['sources'].append({'root': str(root), 'files': rows})
    for path, expected in before.items():
        if digest(Path(path)) != expected:
            raise RuntimeError(f'input changed during media review: {path}')
    report.update(input_sha256=before, decoded_files=decoded, refused_files=refused, envelope_refused_files=storage_refused)
    output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f'Loose media: {decoded} decoded, {refused} decoder refusals, {storage_refused} envelope refusals; inputs unchanged; no source admission.')
    return int(refused != 0 or storage_refused != 0)


if __name__ == '__main__':
    raise SystemExit(main())
