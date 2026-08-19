use "../../stdlib-salvo/sml-basis/Basis.sml";
fun loop (0, total) = total
  | loop (n, total) = loop (n - 1, total + n);
val _ = print (Int.toString (loop (10, 0)) ^ "\n");
