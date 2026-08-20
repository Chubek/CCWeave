program KMPExample;
var text,pat:String;l:array[1..5]of Integer;i,j:Integer;
begin text:='ababcabcabababd';pat:='ababd';j:=0;for i:=2 to 5 do begin while(j>0)and(pat[i]<>pat[j+1])do j:=l[j];if pat[i]=pat[j+1] then j:=j+1;l[i]:=j end;j:=0;for i:=1 to Length(text) do begin while(j>0)and(text[i]<>pat[j+1])do j:=l[j];if text[i]=pat[j+1]then j:=j+1;if j=5 then begin Writeln(i-4);Exit end end;Writeln(-1) end.
