\page ccweave-manual-42 Chapter 42: Release checklist

Before shipping, build warning-clean, run the test suite, and run sanitizer
coverage for Glue ownership paths. Verify that every IR construct round-trips
and that profile validation rejects prohibited constructs.

Regenerate manifests with `ccw-manifest`, then run `ccw-manifest --check`.
Confirm generated files were not manually edited and dependency revisions
remain pinned in `VERSIONS.lock`.

Build the Doxygen `docs` target and review the manual navigation plus public
API reference. A release is conformant only when its executor, kernels,
manifests, IR, and Oeuph behavior satisfy the stated contracts.

Previous: \subpage ccweave-manual-41 "Documentation workflow" · Back to
\ref mainpage "CCWeave home"
