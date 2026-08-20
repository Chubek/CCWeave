program SudokuExample;
var b:array[1..9,1..9]of Integer;i,j:Integer;
begin for i:=1 to 9 do for j:=1 to 9 do b[i,j]:=0;b[1,1]:=5;b[9,9]:=9;Writeln(b[1,1],' ',b[9,9]) end.
