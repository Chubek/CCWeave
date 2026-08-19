program GenericsExample;
type
  TPair<T, U> = record First: T; Second: U end;
var P: TPair<Integer, string>;
begin P.First := 42; P.Second := 'answer'; Writeln(P.First, ' ', P.Second); end.
