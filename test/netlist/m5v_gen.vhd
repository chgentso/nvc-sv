entity m5v_gleaf is
  port (x : in bit);
end entity;

architecture a of m5v_gleaf is
begin
end architecture;

entity m5v_gtop is
end entity;

architecture rtl of m5v_gtop is
  constant N : natural := 2;
  signal s : bit;
begin
  g1: for i in 0 to N - 1 generate
    h: for j in 0 to 1 generate
      u1: entity work.m5v_gleaf port map (s);
    end generate;
  end generate;

  g2: if N > 1 generate
    u2: entity work.m5v_gleaf port map (s);
  else generate
    u3: entity work.m5v_gleaf port map (s);
  end generate;

  g3: case N generate
    when 1 =>
      u4: entity work.m5v_gleaf port map (s);
    when alt: 2 =>
      u5: entity work.m5v_gleaf port map (s);
    when others =>
      u6: entity work.m5v_gleaf port map (s);
  end generate;

  g4: if a1: N > 5 generate
    u7: entity work.m5v_gleaf port map (s);
  elsif a2: N > 1 generate
    u8: entity work.m5v_gleaf port map (s);
  end generate;
end architecture;
