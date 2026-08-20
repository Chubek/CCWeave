use "Basis.sml";
val text = "ababcabcabababd"; val pattern = "ababd";
fun seek i = if i > String.size text - String.size pattern then ~1
  else if String.substring(text,i,5)=pattern then i else seek (i+1);
val _ = print (Int.toString (seek 0) ^ "\n");
