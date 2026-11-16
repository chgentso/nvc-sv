library ieee;
use ieee.std_logic_1164.all;

entity ng_leaf is
    generic (W    : integer := 4;
             NAME : string := "def";
             V    : std_logic_vector(3 downto 0) := "1010";
             E    : std_logic := '1';
             B    : boolean := false;
             R    : real := 0.5;
             T    : time := 10 ns;
             N    : natural := 2);
end entity;

architecture a of ng_leaf is
begin
end architecture;

-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

entity ng_vhdl is
end entity;

architecture test of ng_vhdl is
    constant K : integer := 3;
begin
    u_all: entity work.ng_leaf
        generic map (W => K + 1, NAME => "xy""z", V => x"3", B => true,
                     T => 2 us, R => 1.0e-3);

    u_def: entity work.ng_leaf;

    g: for i in 0 to 1 generate
        u: entity work.ng_leaf generic map (W => i + 5, N => i * 3);
    end generate;
end architecture;
