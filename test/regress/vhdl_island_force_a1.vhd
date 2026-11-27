-- A VHDL-2008 force through an external name on a signal of a 2-state
-- (--2state=islands) RTL island must reach the island: the values given
-- by the force are std_logic and must be converted to bits.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_island_force_a1_dut is
    port (clk : in std_logic; a : in unsigned(7 downto 0);
          c, d : out unsigned(7 downto 0));
end entity;

architecture rtl of vhdl_island_force_a1_dut is
    signal b, ci, q : unsigned(7 downto 0) := (others => '0');
begin
    p_ff : process (clk) is begin
        if rising_edge(clk) then q <= q + 16; end if;
    end process;
    p_b : process (all) is begin b <= a + 1; end process;
    p_c : process (all) is begin ci <= b + 1; end process;
    p_d : process (all) is begin d <= ci + q; end process;
    c <= ci;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_island_force_a1 is end entity;

architecture test of vhdl_island_force_a1 is
    signal clk : std_logic := '0';
    signal a, c, d : unsigned(7 downto 0) := (others => '0');
begin
    dut : entity work.vhdl_island_force_a1_dut port map (clk, a, c, d);

    p_stim : process is
        procedure check (name : string; got, want : unsigned) is
        begin
            assert got = want
                report name & "=" & to_hstring(got) & " expected "
                & to_hstring(want) severity failure;
        end procedure;
    begin
        a <= x"05";
        wait for 1 ns;
        check("c", c, x"07");
        << signal dut.b : unsigned(7 downto 0) >> <= force x"55";
        wait for 1 ns;
        check("c", c, x"56");
        check("d", d, x"56");
        << signal dut.b : unsigned(7 downto 0) >> <= release;
        wait for 1 ns;
        check("c", c, x"07");
        << signal dut.a : unsigned(7 downto 0) >> <= force x"a0";
        wait for 1 ns;
        check("c", c, x"a2");
        << signal dut.a : unsigned(7 downto 0) >> <= release;
        << signal dut.q : unsigned(7 downto 0) >> <= force x"30";
        wait for 1 ns;
        check("c", c, x"07");
        check("d", d, x"37");
        << signal dut.q : unsigned(7 downto 0) >> <= release;
        wait for 1 ns;
        check("d", d, x"07");
        report "done";
        wait;
    end process;
end architecture;
