#!/usr/bin/env python3

#sexp-fix.py -- fix unbalanced parentheses in an s-expression file.
#Strategy:
#  * Scan the file character by character, ignoring parentheses that occur
#    inside string literals ("..."), character literals (#\x), line
#    comments (; ...), and block comments (#| ... |#, nestable).
#  * An unmatched closing paren is removed.
#  * Missing closing parens are appended at the end of the file.

#Usage:
#  python sexp-fix.py input.lisp              # write result to stdout
#  python sexp-fix.py input.lisp -o out.lisp  # write to a file
#  python sexp-fix.py input.lisp -i           # fix the file in place

import argparse
import sys

OPEN = {"(": ")", "[": "]"}
CLOSE = {")": "(", "]": "["}


def fix_sexp(text: str) -> str:
    out = []
    stack = []          # open-paren chars awaiting a closer
    i, n = 0, len(text)

    in_string = False
    in_line_comment = False
    block_comment_depth = 0

    while i < n:
        c = text[i]

        if in_string:
            out.append(c)
            if c == "\\" and i + 1 < n:      # keep escaped char verbatim
                out.append(text[i + 1])
                i += 2
                continue
            if c == '"':
                in_string = False
            i += 1
            continue

        if in_line_comment:
            out.append(c)
            if c == "\n":
                in_line_comment = False
            i += 1
            continue

        if block_comment_depth > 0:
            if text.startswith("#|", i):
                block_comment_depth += 1
                out.append("#|")
                i += 2
                continue
            if text.startswith("|#", i):
                block_comment_depth -= 1
                out.append("|#")
                i += 2
                continue
            out.append(c)
            i += 1
            continue

        # normal code context
        if c == '"':
            in_string = True
            out.append(c)
        elif c == ";":
            in_line_comment = True
            out.append(c)
        elif text.startswith("#|", i):
            block_comment_depth = 1
            out.append("#|")
            i += 2
            continue
        elif text.startswith("#\\", i):      # character literal, e.g. #\( or #\a
            out.append(text[i:i + 3])
            i += 3
            continue
        elif c in OPEN:
            stack.append(c)
            out.append(c)
        elif c in CLOSE:
            if stack and stack[-1] == CLOSE[c]:
                stack.pop()
                out.append(c)
            else:
                # unmatched closer: drop it
                print(f"removed unmatched '{c}' at offset {i}", file=sys.stderr)
        else:
            out.append(c)
        i += 1

    fixed = "".join(out)

    if stack:
        closers = "".join(OPEN[c] for c in reversed(stack))
        print(f"appended {len(closers)} missing closer(s): {closers}",
              file=sys.stderr)
        fixed = fixed.rstrip("\n") + closers + "\n"

    return fixed


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Fix unbalanced parentheses in an s-expression file.")
    ap.add_argument("file", help="input s-expression file")
    ap.add_argument("-o", "--output", help="output file (default: stdout)")
    ap.add_argument("-i", "--in-place", action="store_true",
                    help="overwrite the input file")
    args = ap.parse_args()

    with open(args.file, encoding="utf-8") as f:
        fixed = fix_sexp(f.read())

    if args.in_place:
        target = args.file
    elif args.output:
        target = args.output
    else:
        sys.stdout.write(fixed)
        return

    with open(target, "w", encoding="utf-8") as f:
        f.write(fixed)
    print(f"wrote {target}", file=sys.stderr)


if __name__ == "__main__":
    main()
