use "Basis.sml";
fun even 0 = true | even n = odd (n - 1)
and odd 0 = false | odd n = even (n - 1);
val _ = print (Bool.toString (even 10) ^ "\n");
