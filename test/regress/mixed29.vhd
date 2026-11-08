library ieee;
use ieee.std_logic_1164.all;

entity mixed29_e is
    port (clk : in std_ulogic;
          v   : in std_ulogic_vector(1 downto 0);
          rise, ev, slice : out integer := 0);
end entity;

architecture test of mixed29_e is
begin

    postponed process (clk) is
        variable r, e : integer := 0;
    begin
        if rising_edge(clk) then
            r := r + 1;
        end if;
        if clk'event then
            e := e + 1;
        end if;
        rise <= r after 1 ns;
        ev <= e after 1 ns;
    end process;

    postponed process (v) is
        variable s : integer := 0;
    begin
        if v(1)'event then
            s := s + 1;
        end if;
        slice <= s after 1 ns;
    end process;

end architecture;
