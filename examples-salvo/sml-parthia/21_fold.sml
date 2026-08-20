use "Basis.sml";
val answer = List.foldl (fn (x, a) => x * a) 1 [1, 2, 3, 4];
val _ = print (Int.toString answer ^ "\n");
