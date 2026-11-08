entity svprec1 is end;
architecture a of svprec1 is
  component svprec1_sub is end component;
  component svprec1_fast is end component;
begin
  u1 : svprec1_sub;
  u2 : svprec1_fast;
end;
