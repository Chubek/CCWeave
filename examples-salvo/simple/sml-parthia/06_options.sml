use "Basis.sml";
val answer = Option.getOpt (SOME 42, 0);
val _ = print (Int.toString answer ^ "\n");
