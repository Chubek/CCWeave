program Records;
type TPoint = record X, Y: Integer end;
var P: TPoint;
begin
  P.X := 3; P.Y := 4;
  Writeln(P.X * P.X + P.Y * P.Y);
end.
