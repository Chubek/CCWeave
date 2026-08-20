use "Basis.sml";
exception Negative;
fun checked x = if x < 0 then raise Negative else x;
val answer = (checked 42) handle Negative => 0;
val _ = print (Int.toString answer ^ "\n");
