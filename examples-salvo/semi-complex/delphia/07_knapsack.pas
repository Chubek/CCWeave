program KnapsackExample;
var w,v,d:array[0..8]of Integer;i,c:Integer;
begin w[1]:=2;w[2]:=3;w[3]:=4;w[4]:=5;v[1]:=3;v[2]:=4;v[3]:=5;v[4]:=6;for i:=1 to 4 do for c:=8 downto w[i] do if d[c-w[i]]+v[i]>d[c]then d[c]:=d[c-w[i]]+v[i];Writeln(d[8]) end.
