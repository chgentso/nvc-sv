library ieee;
use ieee.std_logic_1164.all;
use std.env.all;

entity svfinal9 is
end entity;

architecture test of svfinal9 is
    component svfinal9_sub is
        port ( clk : in std_logic );
    end component;
    signal clk : std_logic := '0';
begin
    u: svfinal9_sub port map (clk);

    clk <= not clk after 5 ns when now < 42 ns;

    process is
    begin
        wait for 50 ns;
        report "vhdl done";
        finish;
    end process;
end architecture;
