#!/usr/bin/env python3
"""mk-projmap.py — generate <project>.map: a project tree annotated with
LLM-written descriptions of every file, powered by llama-cpp-python."""

import os
import sys
import fnmatch
from pathlib import Path

from llama_cpp import Llama

MAX_FILE_CHARS = 6000          # how much of each file we show the model
CTX_SIZE = 8192
ALWAYS_IGNORE = {".git", ".models", ".mapignore", "__pycache__"}

SYSTEM_PROMPT = (
    "You are a code documentation assistant. Given a file's path and its "
    "contents, reply with ONE short sentence (max ~20 words) describing what "
    "the file is and does. No preamble, no markdown, just the sentence."
)


def find_model() -> str:
    model = os.environ.get("MKPROJMAP_MODEL")
    if model:
        if not Path(model).is_file():
            sys.exit(f"error: MKPROJMAP_MODEL points to missing file: {model}")
        return model
    models_dir = Path.cwd() / ".models"
    ggufs = sorted(models_dir.glob("*.gguf"))
    if not ggufs:
        sys.exit("error: MKPROJMAP_MODEL is unset and no .gguf found in ./.models")
    return str(ggufs[0])


def load_ignore_patterns(root: Path) -> list[str]:
    patterns = []
    ignore_file = root / ".mapignore"
    if ignore_file.is_file():
        for line in ignore_file.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if line and not line.startswith("#"):
                patterns.append(line.rstrip("/"))
    return patterns


def is_ignored(rel: Path, patterns: list[str]) -> bool:
    parts = rel.parts
    if any(p in ALWAYS_IGNORE for p in parts):
        return True
    rel_str = rel.as_posix()
    for pat in patterns:
        # match full relative path, any path component, or basename
        if (fnmatch.fnmatch(rel_str, pat)
                or any(fnmatch.fnmatch(part, pat) for part in parts)):
            return True
    return False


def read_text(path: Path) -> str | None:
    """Return file text, or None if the file looks binary."""
    try:
        raw = path.read_bytes()
    except OSError:
        return None
    if b"\x00" in raw[:4096]:
        return None
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("utf-8", errors="replace")


def describe(llm: Llama, rel: Path, text: str) -> str:
    snippet = text[:MAX_FILE_CHARS]
    resp = llm.create_chat_completion(
        messages=[
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user",
             "content": f"File: {rel.as_posix()}\n\n
```\n{snippet}\n
```"},
        ],
        max_tokens=64,
        temperature=0.2,
    )
    desc = resp["choices"][0]["message"]["content"].strip()
    return " ".join(desc.split())  # collapse newlines/whitespace


def build_tree(root: Path, patterns: list[str], llm: Llama,
               map_name: str) -> list[str]:
    lines = [root.name + "/"]

    def walk(directory: Path, prefix: str):
        entries = sorted(
            directory.iterdir(),
            key=lambda p: (p.is_file(), p.name.lower()),
        )
        entries = [
            e for e in entries
            if e.name != map_name
            and not is_ignored(e.relative_to(root), patterns)
        ]
        for i, entry in enumerate(entries):
            last = i == len(entries) - 1
            branch = "└── " if last else "├── "
            child_prefix = prefix + ("    " if last else "│   ")
            rel = entry.relative_to(root)
            if entry.is_dir():
                lines.append(f"{prefix}{branch}{entry.name}/")
                walk(entry, child_prefix)
            else:
                text = read_text(entry)
                if text is None:
                    desc = "(binary file)"
                elif not text.strip():
                    desc = "(empty file)"
                else:
                    print(f"describing {rel.as_posix()} ...", file=sys.stderr)
                    try:
                        desc = describe(llm, rel, text)
                    except Exception as exc:
                        desc = f"(description failed: {exc})"
                lines.append(f"{prefix}{branch}{entry.name}  — {desc}")

    walk(root, "")
    return lines


def main():
    root = Path.cwd()
    model_path = find_model()
    print(f"loading model: {model_path}", file=sys.stderr)
    llm = Llama(model_path=model_path, n_ctx=CTX_SIZE, verbose=False)

    patterns = load_ignore_patterns(root)
    map_name = f"{root.name}.map"
    lines = build_tree(root, patterns, llm, map_name)

    out = root / map_name
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {out}", file=sys.stderr)


if __name__ == "__main__":
    main()
