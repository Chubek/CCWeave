use "Basis.sml";
val items = [(2,3),(3,4),(4,5),(5,6)];
fun best ([],_) = 0
  | best ((w,v)::xs,cap) = if w>cap then best(xs,cap)
    else Int.max(best(xs,cap),v+best(xs,cap-w));
val _ = print (Int.toString (best(items,8)) ^ "\n");
