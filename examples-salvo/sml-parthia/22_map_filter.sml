use "../../stdlib-salvo/sml-basis/Basis.sml";
val doubled = List.map (fn x => x * 2) (List.filter (fn x => x mod 2 = 0) [1,2,3,4]);
val _ = print (Int.toString (List.hd doubled) ^ "\n");
