#!/usr/bin/env python3
"""Map the current directory by describing each C, C++, and Python file with a local LLM."""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path
from typing import List, Set

DEFAULT_SKIP_DIRS: Set[str] = {
    ".git", ".hg", ".svn",
    "__pycache__", ".mypy_cache", ".pytest_cache", ".ruff_cache",
    "node_modules", "bower_components", "vendor",
    "venv", ".venv", "env", ".env",
    "build", "dist", "target", "out", ".next", ".nuxt",
    ".idea", ".vscode", "third_party", "extra",
    ".tox", ".nox", ".cache", "coverage", ".terraform",
}

SOURCE_EXTENSIONS: Set[str] = {
    # Python
    ".py", ".pyi", ".pyw",
    # C
    ".c", ".h",
    # C++
    ".cc", ".cpp", ".cxx", ".c++",
    ".hh", ".hpp", ".hxx", ".h++",
    ".ipp", ".tpp", ".inl",
    ".lua"
}


def is_source_file(path: Path) -> bool:
    return path.suffix.lower() in SOURCE_EXTENSIONS


def collect_files(root: Path, skip_dirs: Set[str]) -> List[Path]:
    files: List[Path] = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in skip_dirs]
        for name in filenames:
            path = Path(dirpath) / name
            if is_source_file(path):
                files.append(path)
    return sorted(files)


def read_text(path: Path, max_read: int) -> str:
    with path.open("r", encoding="utf-8", errors="replace") as fh:
        return fh.read(max_read)


def describe_file(model, path: Path, snippet: str, max_tokens: int) -> str:
    prompt = (
        "<|im_start|>system\n"
        "You are a code-review assistant. Write ONE concise sentence (at most 20 words) "
        "describing what the file does. Reply with only the description.<|im_end|>\n"
        "<|im_start|>user\n"
        f"Filename: {path.name}\n\nContent:\n{snippet}<|im_end|>\n"
        "<|im_start|>assistant\n"
    )
    result = model(
        prompt,
        max_tokens=max_tokens,
        temperature=0.2,
        stop=["<|im_end|>", "<|im_start|>"],
    )
    return result["choices"][0]["text"].strip().replace("\n", " ")


def load_model(n_ctx: int):
    model_path = os.environ.get("MAP_DIRECTORY_MODEL")
    if not model_path:
        sys.exit("MAP_DIRECTORY_MODEL is not set; point it at a .gguf model file.")
    if not Path(model_path).is_file():
        sys.exit(f"Model file not found: {model_path}")

    from llama_cpp import Llama

    print(f"Loading model: {model_path}", file=sys.stderr)
    return Llama(
        model_path=model_path,
        n_ctx=n_ctx,
        n_threads=os.cpu_count() or 4,
        use_mmap=False,
        verbose=False,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-o", "--output", type=Path, help="write the map here instead of stdout")
    parser.add_argument("--max-tokens", type=int, default=80)
    parser.add_argument("--max-read", type=int, default=4096)
    parser.add_argument("--n-ctx", type=int, default=2048)
    parser.add_argument("--limit", type=int, help="stop after N files")
    parser.add_argument("--ignore", action="append", default=[], help="extra directory name to skip")
    args = parser.parse_args()

    root = Path.cwd()
    skip_dirs = DEFAULT_SKIP_DIRS | set(args.ignore)

    print(f"Mapping: {root}", file=sys.stderr)
    files = collect_files(root, skip_dirs)
    if args.limit:
        files = files[: args.limit]
    if not files:
        print("No C, C++, or Python files found.", file=sys.stderr)
        return 0
    print(f"Found {len(files)} file(s). Generating descriptions …", file=sys.stderr)

    model = load_model(args.n_ctx)

    lines: List[str] = []
    for index, path in enumerate(files, start=1):
        rel = path.relative_to(root)
        print(f"[{index}/{len(files)}] {rel}", file=sys.stderr)
        try:
            snippet = read_text(path, args.max_read)
            description = describe_file(model, path, snippet, args.max_tokens)
        except Exception as exc:  # noqa: BLE001 - keep mapping the rest of the tree
            print(f"  error: {exc}", file=sys.stderr)
            description = "(description unavailable)"
        lines.append(f"{rel}: {description}")

    output = "\n".join(lines) + "\n"
    if args.output:
        args.output.write_text(output, encoding="utf-8")
    else:
        sys.stdout.write(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
