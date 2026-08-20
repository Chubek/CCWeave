program DijkstraExample;
var d: array[1..4] of Integer; used: array[1..4] of Boolean; g: array[1..4,1..4] of Integer; i,j,k,u,b: Integer;
begin for i:=1 to 4 do begin d[i]:=999; used[i]:=False end; d[1]:=0;
  g[1,2]:=4;g[1,3]:=1;g[2,1]:=4;g[2,3]:=1;g[2,4]:=2;g[3,1]:=1;g[3,2]:=1;g[3,4]:=5;g[4,2]:=2;g[4,3]:=5;
  for k:=1 to 4 do begin u:=1; while used[u] do u:=u+1; for i:=1 to 4 do if (not used[i]) and (d[i]<d[u]) then u:=i; used[u]:=True;
    for j:=1 to 4 do if (g[u,j]>0) and (d[j]>d[u]+g[u,j]) then d[j]:=d[u]+g[u,j] end; Writeln(d[4]) end.
