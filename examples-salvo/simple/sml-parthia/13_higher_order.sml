use "Basis.sml";
fun apply f x = f x;
val _ = print (Int.toString (apply (fn n => n + 1) 41) ^ "\n");
