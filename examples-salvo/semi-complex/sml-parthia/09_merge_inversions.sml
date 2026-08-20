use "Basis.sml";
val values = [2,4,1,3,5];
fun inv [] = 0 | inv (x::xs) = List.length (List.filter (fn y => x>y) xs) + inv xs;
val _ = print (Int.toString (inv values) ^ "\n");
