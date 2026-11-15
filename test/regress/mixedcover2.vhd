library ieee;
use ieee.std_logic_1164.all;

entity mixedcover2_sub is
    generic ( G : integer := 1 );
    port ( a : in std_logic;
           y : out std_logic );
end entity;

architecture test of mixedcover2_sub is
begin

    p1: process (a) is
    begin
        if a = '1' then
            y <= '1';
        else
            y <= '0';
        end if;
    end process;

    gen: if G > 0 generate
        p2: process (a) is
        begin
            assert a /= 'W';
        end process;
    end generate;

end architecture;
