-- Type mismatch between VHDL external name and Verilog signal names both types
library ieee;
use ieee.std_logic_1164.all;

entity mixedext3 is
end entity;

architecture tb of mixedext3 is
    component mixedext3_dut is
    end component;
begin
    u_dut : mixedext3_dut;

    process is
        alias bad is << signal .mixedext3.u_dut.vec8 : std_logic_vector(3 downto 0) >>;
    begin
        wait for 1 ns;
        report to_hstring(bad);
        wait;
    end process;
end architecture;
