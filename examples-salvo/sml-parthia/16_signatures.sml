use "../../stdlib-salvo/sml-basis/Basis.sml";
signature SHOW = sig val show: int -> string end;
structure Number: SHOW = struct fun show x = Int.toString x end;
val _ = print (Number.show 42 ^ "\n");
