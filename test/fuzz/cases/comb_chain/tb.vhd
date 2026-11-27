library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity fz_tb is
end entity;

architecture sim of fz_tb is
    signal clk : std_logic := '0';
    signal i0  : unsigned(3 downto 0) := (others => '0');
    signal o0  : unsigned(7 downto 0);
begin
    dut : entity work.m0 port map (clk => clk, i0 => i0, o0 => o0);

    -- Raises the clock, then counts zero-delay stages so the monitor can
    -- tell in which delta cycle d changed.
    fz_clock : process is
    begin
        wait for 1 ns;
        for n in 1 to 4 loop
            clk <= '1';
            for i in 1 to 6 loop
                i0 <= to_unsigned(i, 4);
                wait for 0 ns;
            end loop;
            wait for 1 ns;
            clk <= '0';
            wait for 1 ns;
        end loop;
        report "FZ F o0 " & to_string(o0);
        report "FZ END";
        std.env.finish;
    end process;
end architecture;
