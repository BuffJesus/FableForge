#!/usr/bin/env python3
"""Every `forge <cmd>` / `forge-tools <cmd>` a shipped doc names must be a real subcommand.

The docs a stranger reads (README, the user docs the zip ships, docs/modding) are matched
against the usage text the two exes print, so a renamed or hidden command cannot outlive its
mention. A usage line may list several commands (`forge backups | forge restore`), so every
`forge <word>` on a usage line counts, not just the first.

  python tools/check_docs_commands.py            (exit 1 with the offending doc:line list)
"""
import os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS = ["README.md", "docs/RELEASE.md", "docs/FIRST_LEVEL.md", "docs/EDITOR.md", "docs/ENGINE_RULES.md",
        "docs/AUTOMATION.md", "docs/CLI.md", "docs/modding/FMP_FORMAT.md", "docs/modding/LOAD_ORDER.md",
        "docs/modding/MOD_PACKS.md"]
# words that follow `forge` in prose without being a subcommand
PROSE = {"exe", "reads", "writes", "is", "the", "a", "an", "and", "or", "does", "cannot", "never", "output",
         "tools", "restore", "backups"}


def usage_commands(exe: str) -> set[str]:
    out = subprocess.run([os.path.join(ROOT, "build", exe)], capture_output=True, text=True).stdout
    return set(re.findall(r"\bforge(?:-tools)?\s+([a-z][a-z0-9-]*)", out))


def main() -> int:
    forge = usage_commands("forge.exe")
    tools = usage_commands("forge-tools.exe")
    forge |= {"restore", "backups"}   # share a usage line with `|`; the regex above already sees both, kept explicit
    bad = []
    for rel in DOCS:
        path = os.path.join(ROOT, rel)
        if not os.path.exists(path):
            continue
        text = open(path, encoding="utf-8", errors="ignore").read()
        for m in re.finditer(r"(?<![\w-])forge(-tools)?\s+([a-z][a-z0-9-]*)", text):
            tool, sub = m.group(1), m.group(2)
            if sub in PROSE or sub in (tools if tool else forge):
                continue
            bad.append(f"{rel}:{text[:m.start()].count(chr(10)) + 1}: {'forge-tools' if tool else 'forge'} {sub}")
    for b in bad:
        print(b)
    print(f"docs name {len(bad)} unknown subcommand(s)" if bad else f"docs commands OK ({len(forge)} forge, {len(tools)} forge-tools)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
