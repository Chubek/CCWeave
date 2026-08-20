use "Basis.sml";
structure Counter =
struct
  val start = 40
  fun next x = x + 1
end;
val _ = print (Int.toString (Counter.next Counter.start) ^ "\n");
