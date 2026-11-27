-- Hand-written case: a combinational chain of four processes that read each
-- other, observed from outside by an unfused process (delta-stamped).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tb is
end entity;

architecture sim of tb is
    signal clk : std_logic := '0';
    signal q   : unsigned(7 downto 0) := (others => '0');
    signal a, b, c, d : unsigned(7 downto 0) := (others => '0');
    signal tick : natural := 0;
begin
    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            q <= q + 3;
        end if;
    end process;

    p_a : process (all) is
    begin
        a <= q + 1;
    end process;

    p_b : process (all) is
    begin
        b <= a xor q;
    end process;

    p_c : process (all) is
    begin
        c <= b + a;
    end process;

    p_d : process (all) is
    begin
        d <= c - b;
    end process;

    -- Outside observer: reports each event of d together with the delta
    -- counter driven by the stimulus.
    p_mon : process (d) is
    begin
        if d'event then
            report "d=" & to_hstring(d) & " tick=" & integer'image(tick);
        end if;
    end process;

    p_stim : process is
    begin
        wait for 1 ns;
        for n in 1 to 4 loop
            clk <= '1';
            for i in 1 to 6 loop
                tick <= i;
                wait for 0 ns;
            end loop;
            wait for 1 ns;
            clk <= '0';
            wait for 1 ns;
        end loop;
        report "end of test";
        std.env.finish;
    end process;
end architecture;
