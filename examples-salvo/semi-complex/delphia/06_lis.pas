program LISExample;
var a,d:array[1..8]of Integer;i,j,b:Integer;
begin a[1]:=10;a[2]:=9;a[3]:=2;a[4]:=5;a[5]:=3;a[6]:=7;a[7]:=101;a[8]:=18;for i:=1 to 8 do begin d[i]:=1;for j:=1 to i-1 do if(a[j]<a[i])and(d[j]+1>d[i])then d[i]:=d[j]+1;if d[i]>b then b:=d[i] end;Writeln(b) end.
