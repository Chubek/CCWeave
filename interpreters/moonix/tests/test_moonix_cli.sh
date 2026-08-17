#!/bin/sh
set -eu

moonix=$1
output=$("$moonix" -e 'print((17 // 5) + (17 % 5) + (6 & 3))')
test "$output" = "7"

output=$("$moonix" --tier=t1 -e 'local t={x=40}; print(t.x + 2)')
test "$output" = "42"

if "$moonix" -e 'coroutine.create(function() end)' 2>"${TMPDIR:-/tmp}/moonix-cli.err"; then
  exit 1
fi
grep -q "not supported in Moonix v0.1" "${TMPDIR:-/tmp}/moonix-cli.err"
rm -f "${TMPDIR:-/tmp}/moonix-cli.err"
