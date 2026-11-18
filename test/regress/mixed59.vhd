entity mixed59 is
end entity;

architecture test of mixed59 is
  component mixed59_v is
    generic (P : real := 1.0; T : real := 1.0; Q : real := 1.0;
             I : real := 1.0; R : integer := 1);
  end component;
begin
  -- A real value rounds when the parameter is an integer and an
  -- integer converts to a real parameter
  u: component mixed59_v
    generic map (P => 2.25, T => 4.5, Q => 7.5, I => 3.6, R => 6);
end architecture;
