program Pointers;
type PInteger = ^Integer;
var Value: Integer; P: PInteger;
begin
  Value := 41; P := @Value; P^ := P^ + 1;
  Writeln(Value);
end.
