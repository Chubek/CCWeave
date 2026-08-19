program ClassHelperExample;
type
  TStringHelper = record helper for string
    function Shout: string;
  end;
function TStringHelper.Shout: string;
begin Shout := UpperCase(Self); end;
begin Writeln('hello'.Shout); end.
