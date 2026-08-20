program VirtualMethods;
type
  TAnimal = class
    function Sound: string; virtual;
  end;
  TDog = class(TAnimal)
    function Sound: string; override;
  end;
function TAnimal.Sound: string;
begin Sound := '...'; end;
function TDog.Sound: string;
begin Sound := 'woof'; end;
var A: TAnimal;
begin A := TDog.Create; Writeln(A.Sound); A.Free; end.
