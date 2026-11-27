-- Zero-delay assignments that do not change the value of the driver
-- still make the driver active in the next delta cycle
library ieee;
use ieee.std_logic_1164.all;

entity signal39 is
end entity;

architecture test of signal39 is
    signal v    : bit_vector(7 downto 0) := X"00";
    signal l    : std_logic := '0';
    signal w    : bit_vector(3 downto 0) := "1010";
    signal trig : integer := 0;
    signal step : integer := 0;
    signal lowv : integer := 0;
begin

    driver: process is
    begin
        wait for 1 ns;
        v <= X"00";                     -- Whole signal, no change
        l <= '0';
        w <= "1010";                    -- Single nexus, no change
        step <= 1;
        wait for 1 ns;
        v(3 downto 0) <= X"0";          -- Second nexus only, no change
        step <= 2;
        wait for 1 ns;
        v <= X"01";                     -- Changes only the second nexus
        step <= 3;
        wait for 1 ns;
        v(5 downto 2) <= "0000";        -- Crosses the nexus boundary
        step <= 4;
        wait for 1 ns;
        v(5 downto 2) <= "0000";        -- Now on a nexus of its own
        step <= 5;
        wait for 1 ns;
        trig <= 1;
        wait on trig;
        v <= X"01";                     -- No change, the only activity
        wait;
    end process;

    low: process (v(3 downto 0)) is     -- Splits the nexus of v
    begin
        lowv <= lowv + 1;
        report "low event " & to_string(v(3 downto 0));
    end process;

    check: process (step) is
    begin
        if step > 0 then
            report "step " & integer'image(step) & " v'active="
                & boolean'image(v'active) & " v'event="
                & boolean'image(v'event) & " l'active="
                & boolean'image(l'active) & " w'active="
                & boolean'image(w'active) & " v'last_active="
                & time'image(v'last_active);
        end if;
    end process;

    post: postponed process (trig) is
    begin
        report "postponed trig=" & integer'image(trig) & " v'last_active="
            & time'image(v'last_active);
    end process;

end architecture;
