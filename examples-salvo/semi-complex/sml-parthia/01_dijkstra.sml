use "Basis.sml";
val edges = [(1,2,4),(1,3,1),(3,2,1),(2,4,2),(3,4,5)];
fun relax ([], d) = d
  | relax ((a,b,w)::es,d) = relax (es, if a=1 andalso w < List.nth(d,b-1) then List.take(d,b-1) @ [w] @ List.drop(d,b) else d);
val distances = relax (edges,[0,999,999,999]);
val _ = print (Int.toString (List.nth (distances,3)) ^ "\n");
