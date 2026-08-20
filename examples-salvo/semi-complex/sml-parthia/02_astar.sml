use "Basis.sml";
val graph = [(1,2,1),(1,3,4),(2,4,3),(3,4,1)];
val heuristic = [4,3,1,0];
fun score (cost,node) = cost + List.nth (heuristic,node-1);
fun search ([],best) = best
  | search ((n,c)::rest,best) = if n=4 then c else search (rest,best);
val _ = print (Int.toString (search ([(1,0),(2,1),(3,4),(4,4)],999)) ^ "\n");
