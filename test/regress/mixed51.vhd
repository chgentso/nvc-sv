entity mixed51_sub is
  generic (width : integer := 5; depth : natural := 2);
end entity;

architecture a of mixed51_sub is
begin
end architecture;

entity mixed51_e is
  generic (top_gen : integer := -3);
end entity;

architecture a of mixed51_e is
begin
  s: entity work.mixed51_sub generic map (width => 9);
end architecture;
