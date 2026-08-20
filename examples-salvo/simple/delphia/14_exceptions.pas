program Exceptions;
begin
  try
    raise Exception.Create('demo');
  except
    on E: Exception do Writeln(E.Message);
  end;
end.
