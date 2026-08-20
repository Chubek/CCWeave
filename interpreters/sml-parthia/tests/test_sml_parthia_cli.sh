#!/bin/sh
set -eu

sml=$1
script_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
basis_dir=$(CDPATH= cd -- "$script_dir/../../../stdlib-salvo/sml-basis" && pwd)

help=$("$sml" --help)
printf '%s\n' "$help" | grep -q '^Usage: sml-parthia \[OPTIONS\] \[FILE.sml\]$'
printf '%s\n' "$help" | grep -q '^  -h, --help  Show this help text and exit\.$'
printf '%s\n' "$help" | grep -q '^  #load LIB          Load a native library from SML_PARTHIA_PATH\.$'
printf '%s\n' "$help" | grep -q '^as given first, then through each directory in the comma-separated$'

output=$(printf '%s\n' \
  'structure A = struct' \
  'end;; structure B = struct end;;' | "$sml")

count=$(printf '%s\n' "$output" | grep -c '^(\?core-ml' || true)
test "$count" -eq 2

hello=$(mktemp)
printf 'use "Basis.sml";\nval _ = print "hello, sml-parthia\\n";\n' >"$hello"
hello_output=$(SML_PARTHIA_PATH="$basis_dir" \
  "$sml" "$hello")
test "$hello_output" = "hello, sml-parthia"
rm -f "$hello"

if printf '%s\n' 'structure A = struct end' | "$sml" >/dev/null 2>"${TMPDIR:-/tmp}/sml-parthia-repl.err"; then
  exit 1
fi
grep -q 'expected ;;' "${TMPDIR:-/tmp}/sml-parthia-repl.err"
rm -f "${TMPDIR:-/tmp}/sml-parthia-repl.err"

directives=$(printf '%s\n' \
  '#help' \
  'structure A = struct end;;' \
  '#open A' \
  '#quit' | "$sml")
printf '%s\n' "$directives" | grep -q '#open MODULE'
printf '%s\n' "$directives" | grep -q '^structure A'

# use directives resolve bare names through the comma-separated
# SML_PARTHIA_PATH; only the second entry below exists, so the comma list
# itself is exercised.
search=$(mktemp -d)
trap 'rm -rf "$search"' EXIT
printf 'structure Search = struct end\n' > "$search/Found.sml"

found=$(printf '%s\n' \
  'use "Found.sml";;' \
  'structure After = struct end;;' \
  | SML_PARTHIA_PATH="/nonexistent-parthia-dir,$search" "$sml")
found_count=$(printf '%s\n' "$found" | grep -c '^(\?core-ml' || true)
test "$found_count" -eq 2

if printf '%s\n' 'use "Found.sml";;' \
  | env -u SML_PARTHIA_PATH "$sml" >/dev/null 2>&1; then
  exit 1
fi
