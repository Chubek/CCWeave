use "Basis.sml";
val grid = Array.fromList [5,3,0,0,7,0,0,0,0,6,0,0,1,9,5,0,0,0,0,9,8,0,0,0,0,6,0];
fun first [] = 0 | first (x::xs) = if x=0 then 1 else 1+first xs;
val _ = print (Int.toString (first (Array.foldr op:: [] grid)) ^ "\n");
