#!/bin/sh
# Manifest conformance (§4.2): --check passes after generation, fails
# after any manual edit, and capability strings are validated.
set -u

TOOL="$1"
ROOT="$2"

fail() { echo "not ok - manifest-check: $1"; exit 1; }

WORK=$(mktemp -d) || fail "cannot create work dir"
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$WORK/kernels" "$WORK/manifests"
cp "$ROOT"/kernels/*.scm "$WORK/kernels/" || fail "cannot stage kernels"

"$TOOL" --root "$WORK" >/dev/null || fail "generation failed"
"$TOOL" --root "$WORK" --check >/dev/null 2>&1 || fail "--check failed after generation"
if grep -q 'capabilities: \[\]' "$WORK/manifests/Kernel.yaml"; then
    fail "implemented kernels emitted an empty capability set"
fi

# A hand edit must be detected as drift.
echo "      - opt.hand-edited" >> "$WORK/manifests/Kernel.yaml"
if "$TOOL" --root "$WORK" --check >/dev/null 2>&1; then
    fail "--check passed despite a hand-edited manifest"
fi

# Regenerating restores agreement.
"$TOOL" --root "$WORK" >/dev/null || fail "regeneration failed"
"$TOOL" --root "$WORK" --check >/dev/null 2>&1 || fail "--check failed after regeneration"

# A missing manifest is drift, not a pass.
rm -f "$WORK/manifests/Capabilities.yaml"
if "$TOOL" --root "$WORK" --check >/dev/null 2>&1; then
    fail "--check passed with a missing manifest"
fi

# An invalid capability id must be refused (§4.1 grammar).
"$TOOL" --root "$WORK" >/dev/null || fail "regeneration failed"
cat > "$WORK/kernels/bad-capability.scm" <<'SCM'
(define-library (ccweave kernel bad-capability)
  (import (scheme base) (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name . bad-capability) (version . "0.0.1") (description . "invalid id")))
    (define (kernel-capabilities) '(NotAValidCapability))
    (define (kernel-apply capability ir options) ir)))
SCM
if "$TOOL" --root "$WORK" >/dev/null 2>&1; then
    fail "generation accepted a capability that violates the grammar"
fi

echo "ok - manifest-check"
