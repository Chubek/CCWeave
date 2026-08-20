program MergeInversionsExample;
var a:array[1..5]of Integer;i,j,t,n:Integer;
begin a[1]:=2;a[2]:=4;a[3]:=1;a[4]:=3;a[5]:=5;n:=0;for i:=1 to 5 do for j:=i+1 to 5 do if a[i]>a[j] then n:=n+1;for i:=1 to 4 do for j:=i+1 to 5 do if a[j]<a[i] then begin t:=a[i];a[i]:=a[j];a[j]:=t end;Writeln(n) end.
