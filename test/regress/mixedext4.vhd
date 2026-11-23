-- Unsupported Verilog dynamic array produces a clean "sorry" message
library ieee;
use ieee.std_logic_1164.all;

entity mixedext4 is
end entity;

architecture tb of mixedext4 is
    component mixedext4_dut is
    end component;
begin
    u_dut : mixedext4_dut;

    process is
        alias bad is << signal .mixedext4.u_dut.dyn_arr : integer >>;
    begin
        wait for 1 ns;
        report integer'image(bad);
        wait;
    end process;
end architecture;
