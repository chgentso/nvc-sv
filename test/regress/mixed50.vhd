library ieee;
use ieee.std_logic_1164.all;

package mixed50_pkg is
  type inner_t is record
    g : std_logic;
    h : std_logic_vector(0 to 2);
  end record;
  type elem_t is record
    a : integer range -4 to 4;
    b : std_logic_vector(7 downto 4);
  end record;
  type elem_arr_t is array (3 downto 1) of elem_t;
  type word_arr_t is array (1 downto 0) of std_logic_vector(3 downto 0);
  type outer_t is record
    valid : std_logic;
    f     : inner_t;
    vec   : std_logic_vector(7 downto 0);
    arr   : elem_arr_t;
    words : word_arr_t;
    flag  : boolean;
    cnt   : integer;
  end record;
  type outer_arr_t is array (2 to 4) of outer_t;
end package;

library ieee;
use ieee.std_logic_1164.all;
use work.mixed50_pkg.all;

entity mixed50_sub is
  generic (width : integer := 5);
  port (pr : in outer_t);
end entity;

architecture a of mixed50_sub is
begin
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use work.mixed50_pkg.all;

entity mixed50_e is
end entity;

architecture a of mixed50_e is
  signal o : outer_t := ('0', ('1', "101"), x"A5",
                         ((1, x"1"), (-2, x"2"), (3, x"3")),
                         (x"5", x"6"), true, 42);
  signal ww : word_arr_t := (x"A", x"3");
  signal oa : outer_arr_t := (others => ('1', ('0', "010"), x"0F",
                                         ((0, x"0"), (0, x"0"), (0, x"0")),
                                         (x"0", x"0"), false, 7));
begin
  s: entity work.mixed50_sub generic map (width => 9) port map (pr => o);

  process begin
    wait for 5 ns;
    o.valid <= '1';
    o.f.g <= 'Z';
    wait for 1 ns;
    o.f.h <= "110";
    wait for 1 ns;
    o.vec <= x"3C";
    wait for 1 ns;
    o.words(1) <= x"F";
    wait for 1 ns;
    o.arr(2).a <= 4;
    o.arr(2).b <= "1010";
    wait for 1 ns;
    o.cnt <= -9;
    wait for 1 ns;
    oa(3).f.h <= "111";
    oa(3).arr(1).b <= "1111";
    oa(4).vec <= x"81";
    wait for 1 ns;
    o.vec(0) <= 'X';
    wait;
  end process;
end architecture;
