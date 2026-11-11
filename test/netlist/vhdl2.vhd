library ieee;
use ieee.std_logic_1164.all;

package vhdl2_pkg is
    type pair_t is record
        a : std_logic_vector(3 downto 0);
        b : std_logic;
    end record;
    type pair_array_t is array (0 to 1) of pair_t;
end package;

-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use work.vhdl2_pkg.all;

entity sink is
    port ( p : in pair_t; q : out std_logic );
end entity;

architecture rtl of sink is
begin
    q <= p.b and p.a(0);
end architecture;

-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use work.vhdl2_pkg.all;

entity vhdl2 is
end entity;

architecture test of vhdl2 is
    signal r    : pair_t;
    signal arr  : pair_array_t;
    signal q0, q1 : std_logic;
begin
    r.a <= "1010";
    r.b <= '1';
    arr(0) <= r;
    arr(1).b <= q0;

    u0: entity work.sink port map ( p => r, q => q0 );
    u1: entity work.sink port map ( p => arr(0), q => q1 );
end architecture;
