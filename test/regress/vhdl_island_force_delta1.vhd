-- A force or release issued by a VHDL process takes effect in the next
-- delta cycle (IEEE 1076-2008 10.5.2.2): the readers of a forced island
-- signal must see the new value in the same delta cycle as with
-- --rtl-islands=off, also when the island runs in the delta of the force.
-- The gold file was produced with --rtl-islands=off.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_island_force_delta1_dut is
    port (clk : in std_logic; a : in unsigned(7 downto 0);
          c, d : out unsigned(7 downto 0));
end entity;

architecture rtl of vhdl_island_force_delta1_dut is
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

entity vhdl_island_force_delta1 is end entity;

architecture test of vhdl_island_force_delta1 is
    signal clk : std_logic := '0';
    signal a, c, d : unsigned(7 downto 0) := (others => '0');
    signal tick : natural := 0;
begin
    dut : entity work.vhdl_island_force_delta1_dut port map (clk, a, c, d);

    p_log : process (c, d) is
    begin
        if c'event then report integer'image(tick) & " c=" & to_hstring(c); end if;
        if d'event then report integer'image(tick) & " d=" & to_hstring(d); end if;
    end process;

    p_stim : process is
        procedure settle is begin
            for i in 1 to 6 loop tick <= i; wait for 0 ns; end loop;
            wait for 1 ns; tick <= 0; wait for 0 ns;
        end procedure;
    begin
        wait for 1 ns;
        a <= x"05"; settle;
        report "force";
        << signal dut.b : unsigned(7 downto 0) >> <= force x"55"; settle;
        a <= x"09"; settle;
        report "release";
        << signal dut.b : unsigned(7 downto 0) >> <= release; settle;
        report "force ci";
        << signal dut.ci : unsigned(7 downto 0) >> <= force x"77"; settle;
        clk <= '1'; settle;
        << signal dut.ci : unsigned(7 downto 0) >> <= release; settle;
        report "force with input";
        a <= x"0c";
        << signal dut.b : unsigned(7 downto 0) >> <= force x"66"; settle;
        << signal dut.b : unsigned(7 downto 0) >> <= release; settle;
        report "force q";
        << signal dut.q : unsigned(7 downto 0) >> <= force x"30"; settle;
        clk <= '0'; settle; clk <= '1'; settle;
        << signal dut.q : unsigned(7 downto 0) >> <= release; settle;
        clk <= '0'; settle; clk <= '1'; settle;
        report "force a port";
        << signal dut.a : unsigned(7 downto 0) >> <= force x"40"; settle;
        a <= x"41"; settle;
        << signal dut.a : unsigned(7 downto 0) >> <= release; settle;
        report "done";
        wait;
    end process;
end architecture;
