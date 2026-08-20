program SetsExample;
type TLetter = (A, B, C, D);
var S: set of TLetter;
begin
  S := [A, C];
  if C in S then Writeln('contains C');
end.
