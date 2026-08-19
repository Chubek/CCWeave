program CaseExample;
var N: Integer;
begin
  N := 2;
  case N of
    1: Writeln('one');
    2: Writeln('two');
    else Writeln('other');
  end;
end.
