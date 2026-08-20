use "Basis.sml";
fun fact n = if n < 2 then 1 else n * fact (n - 1);
val _ = print (Int.toString (fact 6) ^ "\n");
