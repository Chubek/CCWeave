use "../../stdlib-salvo/sml-basis/Basis.sml";
val value = 2;
val text = case value of 1 => "one" | 2 => "two" | _ => "other";
val _ = print (text ^ "\n");
