program Classes;
type
  TCounter = class
    Value: Integer;
    procedure Inc;
  end;
procedure TCounter.Inc;
begin
  Value := Value + 1;
end;
var C: TCounter;
begin
  C := TCounter.Create; C.Inc; Writeln(C.Value); C.Free;
end.
