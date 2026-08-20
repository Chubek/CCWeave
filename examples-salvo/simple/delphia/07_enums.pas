program Enums;
type TColor = (Red, Green, Blue);
var C: TColor;
begin
  C := Green;
  if C = Green then Writeln('green');
end.
