program Properties;
type
  TBox = class
  private FValue: Integer;
    function GetValue: Integer;
    procedure SetValue(V: Integer);
  public property Value: Integer read GetValue write SetValue;
  end;
function TBox.GetValue: Integer;
begin GetValue := FValue; end;
procedure TBox.SetValue(V: Integer);
begin FValue := V; end;
var B: TBox;
begin B := TBox.Create; B.Value := 42; Writeln(B.Value); B.Free; end.
