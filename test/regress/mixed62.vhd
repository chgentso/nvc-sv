entity mixed62_sub is
  generic (G_SCALE : real := 2.0);
  port (
    r_in  : in  real;
    r_out : out real
  );
end entity;

architecture rtl of mixed62_sub is
  component mixed62_leaf is
    port (
      a : in  real;
      b : out real
    );
  end component;

  signal scaled : real;
begin
  scaled <= r_in * G_SCALE;

  u_leaf : mixed62_leaf
    port map (
      a => scaled,
      b => r_out
    );
end architecture;
