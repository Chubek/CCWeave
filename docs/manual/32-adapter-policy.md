\page ccweave-manual-32 Chapter 32: Adapter policy

Swaff exposes two policies for Tree-sitter `ERROR` and `MISSING` nodes:
reject the lowering, or recover by skipping the affected subtree and
recording a diagnostic.

The policy must be deliberate; silently ignoring malformed CST nodes breaks
the adapter contract. Read the resulting `ccw_swaff_report` for error,
missing, recovered, unsupported, declaration, statement, and function
counts.

Lowering returns a fresh module on success. On rejection or other failure it
returns no module and gives a caller-owned error message where available.

Previous: \subpage ccweave-manual-31 "Swaff overview" · Next:
\subpage ccweave-manual-33 "C frontend"
