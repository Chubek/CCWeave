\page ccweave-manual-35 Chapter 35: Host integration

An embedding host creates a canonical Weave IR module, creates and verifies
an executor, registers the Core Accessor Set, loads a kernel, and invokes a
capability with the module handle and optional `key=value` options.

Registering `ccw_host_register_core_accessors` provides reflection,
navigation, inspection, and builder-based mutation. Hosts may add extensions
but portable kernels must test for them with `glue-has?`.

The host can install an edit hook with `ccw_host_set_edit_hook` to log or
reject replacement, insertion, deletion, and block-deletion requests.

Previous: \subpage ccweave-manual-34 "S7 executor" · Next:
\subpage ccweave-manual-36 "Errors"
