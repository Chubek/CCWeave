\page ccweave-manual-12 Chapter 12: Generated manifests

`ccw-manifest` loads kernels through the executor and calls their live
metadata procedures. It emits `manifests/Kernel.yaml`, a per-kernel index,
and `manifests/Capabilities.yaml`, its inverted capability index.

Both files are generated artifacts and must carry their generated-file
headers. Regenerate them with the tool; never edit them by hand.

Use `ccw-manifest --check` in continuous integration. It regenerates
in-memory and compares the result with the checked-in files, making drift a
build failure rather than a runtime surprise.

Previous: \subpage ccweave-manual-11 "Capabilities" · Next:
\subpage ccweave-manual-13 "Writing a kernel"
