program RTTIExample;
uses TypInfo;
type TKind = (tkOne, tkTwo);
begin
  Writeln(GetEnumName(TypeInfo(TKind), Ord(tkTwo)));
end.
