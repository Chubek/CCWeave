use "../../stdlib-salvo/sml-basis/Basis.sml";
(* The native companion is exposed by stdlib-salvo/sml-basis. *)
val _ = print (Int.toString (String.size SMLBasis.nativeLibrary) ^ "\n");
