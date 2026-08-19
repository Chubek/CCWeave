program NestedProcedures;
var Base: Integer;
procedure Add(Value: Integer);
begin Base := Base + Value end;
begin Base := 10; Add(32); Writeln(Base); end.
