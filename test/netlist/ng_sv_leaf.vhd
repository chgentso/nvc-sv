library ieee;
use ieee.std_logic_1164.all;

entity ng_sv_leaf is
    generic (W    : integer := 4;
             NAME : string := "def";
             V    : std_logic_vector(3 downto 0) := "1010";
             B    : boolean := false;
             R    : real := 0.5);
    port (o : out std_logic);
end entity;

architecture a of ng_sv_leaf is
begin
    o <= '1';
end architecture;
