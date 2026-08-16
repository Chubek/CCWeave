\page ccweave-manual-01 Chapter 1: Orientation

CCWeave is a modular compiler infrastructure whose stable center is Weave
IR. Source frontends lower programs into the IR; kernels and Oeuph transform
it; executors bridge portable Scheme code to a host-owned module.

The manual describes the contracts implemented by this repository. The
CCWeave Specification v0.1 and `glue/GlueSTD.h` remain normative, with the
ABI header taking precedence on ABI questions.

Read this manual in order when integrating CCWeave. Use the generated API
reference for individual C declarations.

Next: \subpage ccweave-manual-02 "Chapter 2: Architecture and boundaries"
