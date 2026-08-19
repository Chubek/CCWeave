program Recursion;
function Fact(N: Integer): Integer;
begin
  if N < 2 then Fact := 1 else Fact := N * Fact(N - 1);
end;
begin Writeln(Fact(6)); end.
