program Arrays;
var Values: array[1..4] of Integer;
    I, Total: Integer;
begin
  Values[1] := 2; Values[2] := 4; Values[3] := 6; Values[4] := 8;
  Total := 0;
  for I := 1 to 4 do Total := Total + Values[I];
  Writeln(Total);
end.
