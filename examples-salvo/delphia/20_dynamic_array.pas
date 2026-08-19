program DynamicArrayExample;
var Values: array of Integer; I, Total: Integer;
begin
  SetLength(Values, 3);
  for I := 0 to High(Values) do Values[I] := I + 1;
  Total := 0;
  for I := 0 to High(Values) do Total := Total + Values[I];
  Writeln(Total);
end.
