use "Basis.sml";
val edges = [(1,2,1),(2,3,2),(1,3,3),(3,4,1),(2,4,4)];
fun root (p,x) = if List.nth(p,x-1)=x then x else root (p,List.nth(p,x-1));
fun choose ([],_,total) = total
  | choose ((a,b,w)::es,p,total) =
      if root(p,a)=root(p,b) then choose(es,p,total)
      else choose(es,p,total+w);
val _ = print (Int.toString (choose (edges,[1,2,3,4],0)) ^ "\n");
