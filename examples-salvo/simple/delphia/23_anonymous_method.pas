program AnonymousMethodExample;
type TAction = reference to procedure;
var Action: TAction;
begin
  Action := procedure begin Writeln('called') end;
  Action();
end.
