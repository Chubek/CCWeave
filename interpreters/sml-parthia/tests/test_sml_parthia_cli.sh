#!/bin/sh
set -eu

sml=$1

output=$(printf '%s\n' \
  'structure A = struct' \
  'end;; structure B = struct end;;' | "$sml")

count=$(printf '%s\n' "$output" | grep -c '^(\?core-ml' || true)
test "$count" -eq 2

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
