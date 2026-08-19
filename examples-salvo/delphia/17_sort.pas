program SortExample;
var A: array[1..5] of Integer; I, J, T: Integer;
begin
  A[1] := 5; A[2] := 1; A[3] := 4; A[4] := 2; A[5] := 3;
  for I := 1 to 4 do for J := I + 1 to 5 do
    if A[J] < A[I] then begin T := A[I]; A[I] := A[J]; A[J] := T end;
  Writeln(A[1], ' ', A[5]);
end.
