program KruskalExample;
type Edge=record a,b,w:Integer end;
var e:array[1..5]of Edge;p:array[1..4]of Integer;i,j,t,x,y:Integer;z:Edge;
function Find(a:Integer):Integer;begin while p[a]<>a do begin p[a]:=p[p[a]];a:=p[a] end;Find:=a end;
begin e[1].a:=1;e[1].b:=2;e[1].w:=1;e[2].a:=2;e[2].b:=3;e[2].w:=2;e[3].a:=1;e[3].b:=3;e[3].w:=3;e[4].a:=3;e[4].b:=4;e[4].w:=1;e[5].a:=2;e[5].b:=4;e[5].w:=4;for i:=1 to 4 do p[i]:=i;
for i:=1 to 4 do for j:=i+1 to 5 do if e[j].w<e[i].w then begin z:=e[i];e[i]:=e[j];e[j]:=z end;t:=0;for i:=1 to 5 do begin x:=Find(e[i].a);y:=Find(e[i].b);if x<>y then begin p[x]:=y;t:=t+e[i].w end end;Writeln(t) end.
