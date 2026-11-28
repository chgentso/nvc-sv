library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity fz_tb is
end entity;

architecture sim of fz_tb is
    signal clk : std_logic := '0';
    signal rst : std_logic := '1';
    signal o0, o1, o3 : unsigned(7 downto 0);
    signal o2 : unsigned(79 downto 0);
begin
    dut : entity work.m0 port map (clk => clk, rst => rst, o0 => o0,
                                   o1 => o1, o2 => o2, o3 => o3);

    fz_clock : process is
    begin
        for c in 0 to 14 loop
            wait for 10 ns;
            clk <= '1';
            wait for 10 ns;
            clk <= '0';
            if c = 1 then
                rst <= '0';
            end if;
        end loop;
        wait for 5 ns;
        report "FZ F o0 " & to_string(o0);
        report "FZ F o2 " & to_string(o2);
        report "FZ END";
        std.env.finish;
    end process;

    fz_sample : process is
    begin
        for c in 0 to 13 loop
            wait for 15 ns;
            report "FZ S " & integer'image(c) & " o0=" & to_string(o0)
                & " o3=" & to_string(o3);
            wait for 5 ns;
        end loop;
        wait;
    end process;
end architecture;
