use "Basis.sml";
val values = [10,9,2,5,3,7,101,18];
fun extend (x,[]) = [x]
  | extend (x,y::ys) = if x<y then x::y::ys else y::extend(x,ys);
fun lis [] = [] | lis (x::xs) = extend(x,lis xs);
val _ = print (Int.toString (List.length (lis values)) ^ "\n");
