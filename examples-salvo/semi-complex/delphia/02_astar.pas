program AStarExample;
var g: array[1..4,1..4] of Integer; h,d: array[1..4] of Integer; used: array[1..4] of Boolean; i,j,k,u: Integer;
begin for i:=1 to 4 do begin d[i]:=999;used[i]:=False end;d[1]:=0;h[1]:=4;h[2]:=3;h[3]:=1;h[4]:=0;
  g[1,2]:=1;g[1,3]:=4;g[2,1]:=1;g[2,3]:=3;g[2,4]:=3;g[3,1]:=4;g[3,2]:=3;g[3,4]:=1;g[4,2]:=3;g[4,3]:=1;
  for k:=1 to 4 do begin u:=1;while used[u] do u:=u+1;for i:=1 to 4 do if(not used[i])and(d[i]+h[i]<d[u]+h[u])then u:=i;used[u]:=True;
    for j:=1 to 4 do if(g[u,j]>0)and(d[j]>d[u]+g[u,j])then d[j]:=d[u]+g[u,j] end;Writeln(d[4]) end.
