program FloydWarshallExample;
var d:array[1..4,1..4]of Integer;i,j,k:Integer;
begin d[1,1]:=0;d[1,2]:=3;d[1,3]:=999;d[1,4]:=7;d[2,1]:=8;d[2,2]:=0;d[2,3]:=2;d[2,4]:=999;d[3,1]:=5;d[3,2]:=999;d[3,3]:=0;d[3,4]:=1;d[4,1]:=2;d[4,2]:=999;d[4,3]:=999;d[4,4]:=0;for k:=1 to 4 do for i:=1 to 4 do for j:=1 to 4 do if d[i,k]+d[k,j]<d[i,j]then d[i,j]:=d[i,k]+d[k,j];Writeln(d[1,4]) end.
