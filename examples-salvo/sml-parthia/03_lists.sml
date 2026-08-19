use "../../stdlib-salvo/sml-basis/Basis.sml";
val values = [2, 4, 6, 8];
val total = List.foldl (fn (x, a) => x + a) 0 values;
val _ = print (Int.toString total ^ "\n");
