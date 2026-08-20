program TopologicalSortExample;
var g:array[1..4,1..4]of Boolean;inD,q:array[1..4]of Integer;i,j,h,t,u:Integer;
begin g[1,2]:=True;g[1,3]:=True;g[2,4]:=True;g[3,4]:=True;for i:=1 to 4 do for j:=1 to 4 do if g[i,j] then inD[j]:=inD[j]+1;for i:=1 to 4 do if inD[i]=0 then begin t:=t+1;q[t]:=i end;h:=1;while h<=t do begin u:=q[h];h:=h+1;Write(u,' ');for i:=1 to 4 do if g[u,i] then begin inD[i]:=inD[i]-1;if inD[i]=0 then begin t:=t+1;q[t]:=i end end end;Writeln end.
