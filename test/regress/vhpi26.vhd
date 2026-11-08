-- VHDL part of test vhpi26: a counter instantiated from Verilog

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhpi26_cnt is
    port (
        clk : in  std_logic;
        q   : out std_logic_vector(7 downto 0) );
end entity;

architecture rtl of vhpi26_cnt is
    signal cnt : natural range 0 to 255 := 0;
begin
    process (clk) is
    begin
        if rising_edge(clk) then
            cnt <= cnt + 1;
        end if;
    end process;

    q <= std_logic_vector(to_unsigned(cnt, 8));
end architecture;
