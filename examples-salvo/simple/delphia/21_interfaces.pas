program InterfacesExample;
type
  IGreeter = interface
    procedure Greet;
  end;
  TGreeter = class(TInterfacedObject, IGreeter)
    procedure Greet;
  end;
procedure TGreeter.Greet;
begin Writeln('hello'); end;
var G: IGreeter;
begin G := TGreeter.Create; G.Greet; end.
