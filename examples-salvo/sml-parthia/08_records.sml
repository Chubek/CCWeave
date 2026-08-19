use "../../stdlib-salvo/sml-basis/Basis.sml";
type point = {x: int, y: int};
val p: point = {x = 3, y = 4};
val _ = print (Int.toString (#x p * #x p + #y p * #y p) ^ "\n");
