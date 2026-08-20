use "Basis.sml";
val deps = [(1,2),(1,3),(2,4),(3,4)];
fun order ([],seen) = seen
  | order ((a,b)::es,seen) = order(es, if List.exists (fn x => x=b) seen then seen else seen @ [b]);
val result = 1 :: order (deps,[]);
val _ = print (String.concatWith "," (List.map Int.toString result) ^ "\n");
