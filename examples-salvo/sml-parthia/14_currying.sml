use "../../stdlib-salvo/sml-basis/Basis.sml";
fun add x y = x + y;
val addTen = add 10;
val _ = print (Int.toString (addTen 32) ^ "\n");
