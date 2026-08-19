# SML Basis Salvo

This is the Parthia Basis seed. `Basis.sml` contains portable, source-level
definitions (the part compiled by Parthia); operations requiring host services
are supplied by `sml_basis_native` and the Parthia scalar FFI.

Source directives accepted by Parthia include:

```sml
use "Basis.sml";
(*#load "libsml_basis.so"*)
```

`load`/`CM.make` directives load a native extension when a runtime is supplied.
