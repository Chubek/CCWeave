use "Basis.sml";
val matrix = [[0,3,999,7],[8,0,2,999],[5,999,0,1],[2,999,999,0]];
fun improve (a,b) = List.map (fn row => List.map (fn x => Int.min(x,a+b) ) row) matrix;
val _ = print (Int.toString (List.nth(List.nth(matrix,0),3)) ^ "\n");
