use "Basis.sml";
val values = Array.fromList [1, 2, 3];
val _ = Array.update (values, 1, 40);
val _ = print (Int.toString (Array.sub (values, 1) + 2) ^ "\n");
