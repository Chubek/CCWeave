use "Basis.sml";
val cell = ref 0;
val _ = cell := !cell + 42;
val _ = print (Int.toString (!cell) ^ "\n");
