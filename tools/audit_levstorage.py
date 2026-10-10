"""Read-only LEV storage review of loose files and WAD payloads.

WAD envelope metadata and symbolic definition resolution are outside this gate.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool', type=Path, default=repo / 'build/fableforge_levstorage_tests.exe')
    parser.add_argument('--source', type=Path, action='append', required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    roots = [path.resolve() for path in args.source]
    tool, output = args.tool.resolve(), args.out.resolve()
    if output in (tool, Path(__file__).resolve()) or any(output.is_relative_to(root) for root in roots):
        parser.error('report must be outside input roots and must not replace audit tooling')
    tool_hash = digest(tool)
    report = {'scope': __doc__, 'tool_sha256': tool_hash, 'sources': []}
    for root in roots:
        levels = root / 'data/Levels'
        files = sorted(path for path in levels.rglob('*') if path.is_file() and path.suffix.lower() in ('.lev', '.wad'))
        if not files:
            raise RuntimeError(f'no LEV/WAD inputs under {levels}')
        before = {str(path): digest(path) for path in files}
        source = {'root': str(root), 'input_sha256': before, 'reviews': []}
        for path in [levels, *(path for path in files if path.suffix.lower() == '.wad')]:
            run = subprocess.run([str(tool), str(path)], capture_output=True, text=True, encoding='utf-8')
            if run.returncode:
                raise RuntimeError(f'LEV review failed ({run.returncode}): {run.stderr.strip()}')
            row = json.loads(run.stdout)
            row['path'] = str(path)
            source['reviews'].append(row)
            print(row, flush=True)
        if any(digest(Path(path)) != value for path, value in before.items()):
            raise RuntimeError(f'input changed during review: {root}')
        report['sources'].append(source)
    if digest(tool) != tool_hash:
        raise RuntimeError('audit tool changed during review')
    report['script_sha256'] = digest(Path(__file__))
    output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
