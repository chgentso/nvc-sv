-- A transaction that leaves the driver value unchanged from a postponed
-- process in code compiled by LLVM must still be reported as an error
library ieee;
use ieee.std_logic_1164.all;

entity signal42 is
end entity;

architecture test of signal42 is
    signal v   : std_logic_vector(11 downto 0) := X"abc";
    signal n   : natural := 3;
    signal clk : bit := '0';
begin

    clk <= not clk after 1 ns when now < 10 ns;

    -- Runs in delta cycles first to give the code generator a chance
    busy: process (clk) is
    begin
        n <= 3;
    end process;

    post: postponed process (clk) is
    begin
        if now >= 4 ns then
            v <= X"abc";
        end if;
    end process;

end architecture;
