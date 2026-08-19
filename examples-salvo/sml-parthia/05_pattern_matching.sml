use "../../stdlib-salvo/sml-basis/Basis.sml";
fun describe [] = "empty"
  | describe (_ :: _) = "nonempty";
val _ = print (describe [1, 2] ^ "\n");
