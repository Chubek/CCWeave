use "Basis.sml";
datatype color = Red | Green | Blue;
fun name Green = "green" | name Red = "red" | name Blue = "blue";
val _ = print (name Green ^ "\n");
