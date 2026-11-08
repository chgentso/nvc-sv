library ieee;
use ieee.std_logic_1164.all;

entity mixed26_e is
    port (clk : in std_ulogic;
          v   : in std_ulogic_vector(1 downto 0);
          rise, fall, ev, slice : out integer := 0);
end entity;

architecture test of mixed26_e is
begin

    process (clk) is
        variable r, f, e : integer := 0;
    begin
        if clk'event then
            e := e + 1;
        end if;
        if rising_edge(clk) then
            r := r + 1;
        end if;
        if falling_edge(clk) then
            f := f + 1;
        end if;
        rise <= r after 1 ns;
        fall <= f after 1 ns;
        ev <= e after 1 ns;
    end process;

    process (v) is
        variable s : integer := 0;
    begin
        if v(1)'event then
            s := s + 1;
        end if;
        slice <= s after 1 ns;
    end process;

end architecture;
