-- VHDL part of test vpi9: an incrementer instantiated from Verilog

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vpi9_inc is
    port (
        a : in  std_logic_vector(7 downto 0);
        y : out std_logic_vector(7 downto 0) );
end entity;

architecture rtl of vpi9_inc is
begin
    y <= std_logic_vector(unsigned(a) + 1);
end architecture;
