"""Read-only complete STB storage review; does not admit a mod or resolve assets."""
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
    parser.add_argument('--tool', type=Path, default=repo / 'build/fableforge_stbstorage_tests.exe')
    parser.add_argument('--bank', type=Path, action='append', required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    banks = [path.resolve() for path in args.bank]
    tool, output = args.tool.resolve(), args.out.resolve()
    if output in [tool, Path(__file__).resolve(), *banks]:
        parser.error('report must not replace inputs or the audit tool')
    tool_hash = digest(tool)
    report = {'scope': __doc__, 'tool_sha256': tool_hash, 'banks': []}
    for bank in banks:
        before = digest(bank)
        run = subprocess.run([str(tool), str(bank)], capture_output=True, text=True, encoding='utf-8')
        if digest(bank) != before:
            raise RuntimeError(f'input changed during review: {bank}')
        if run.returncode:
            raise RuntimeError(f'STB review failed ({run.returncode}): {run.stderr.strip()}')
        row = json.loads(run.stdout)
        row.update(path=str(bank), input_sha256=before)
        report['banks'].append(row)
        print(bank, row, flush=True)
    if digest(tool) != tool_hash:
        raise RuntimeError('audit tool changed during review')
    report['script_sha256'] = digest(Path(__file__))
    output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
