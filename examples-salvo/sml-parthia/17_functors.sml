use "Basis.sml";
signature VALUE = sig val value: int end;
functor Twice (X: VALUE) = struct val value = X.value * 2 end;
structure Answer = Twice (struct val value = 21 end);
val _ = print (Int.toString Answer.value ^ "\n");
