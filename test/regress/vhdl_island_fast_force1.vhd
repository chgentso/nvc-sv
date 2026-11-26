library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_fast_force_dut is
    port (
        clk         : in  std_logic;
        din         : in  unsigned(7 downto 0);
        sib_comb    : out unsigned(7 downto 0);
        sib_ff      : out unsigned(7 downto 0);
        pruned_comb : out unsigned(7 downto 0);
        pruned_ff   : out unsigned(7 downto 0)
    );
end entity;

architecture rtl of vhdl_fast_force_dut is
    signal q_reg : unsigned(7 downto 0) := x"10";
begin
    p_comb : process (din, q_reg) is
    begin
        sib_comb    <= din xor x"55";
        pruned_comb <= din xor x"aa";
    end process;

    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            q_reg     <= din + x"01";
            sib_ff    <= din + x"02";
            pruned_ff <= din + x"03";
        end if;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_island_fast_force1 is
end entity;

architecture test of vhdl_island_fast_force1 is
    signal clk      : std_logic := '0';
    signal din      : unsigned(7 downto 0) := x"11";
    signal sib_comb : unsigned(7 downto 0);
    signal sib_ff   : unsigned(7 downto 0);
    signal go_force : boolean := false;
    signal done     : boolean := false;
begin
    dut : entity work.vhdl_fast_force_dut
        port map (
            clk         => clk,
            din         => din,
            sib_comb    => sib_comb,
            sib_ff      => sib_ff,
            pruned_comb => open,
            pruned_ff   => open
        );

    -- External names are evaluated only after go_force becomes true,
    -- so pruned_comb and pruned_ff start out PRUNED at reset.
    p_force : process is
    begin
        wait until go_force;

        -- Read initial values computed while pruned
        assert << signal dut.pruned_comb : unsigned(7 downto 0) >> = (x"11" xor x"aa")
            report "initial pruned_comb mismatch";
        assert << signal dut.pruned_ff : unsigned(7 downto 0) >> = (x"11" + x"03")
            report "initial pruned_ff mismatch";

        -- Force both pruned outputs
        << signal dut.pruned_comb : unsigned(7 downto 0) >> <= force x"ee";
        << signal dut.pruned_ff   : unsigned(7 downto 0) >> <= force x"dd";
        wait for 1 ns;
        assert << signal dut.pruned_comb : unsigned(7 downto 0) >> = x"ee"
            report "forced pruned_comb mismatch";
        assert << signal dut.pruned_ff : unsigned(7 downto 0) >> = x"dd"
            report "forced pruned_ff mismatch";

        -- Wait while stimulus changes din to 0x22 and clocks (sibling changes)
        wait for 14 ns;
        assert << signal dut.pruned_comb : unsigned(7 downto 0) >> = x"ee"
            report "pruned_comb did not hold force";
        assert << signal dut.pruned_ff : unsigned(7 downto 0) >> = x"dd"
            report "pruned_ff did not hold force";

        -- Release both: must restore driven values for din=0x22
        << signal dut.pruned_comb : unsigned(7 downto 0) >> <= release;
        << signal dut.pruned_ff   : unsigned(7 downto 0) >> <= release;
        wait for 1 ns;
        assert << signal dut.pruned_comb : unsigned(7 downto 0) >> = (x"22" xor x"aa")
            report "released pruned_comb mismatch";
        assert << signal dut.pruned_ff : unsigned(7 downto 0) >> = (x"22" + x"03")
            report "released pruned_ff mismatch";

        -- Wait while stimulus returns din to 0x11 and clocks
        wait for 14 ns;
        assert << signal dut.pruned_comb : unsigned(7 downto 0) >> = (x"11" xor x"aa")
            report "post-release pruned_comb mismatch";
        assert << signal dut.pruned_ff : unsigned(7 downto 0) >> = (x"11" + x"03")
            report "post-release pruned_ff mismatch";
        done <= true;
        wait;
    end process;

    p_stim : process is
    begin
        -- Cycle 1 with din = 0x11 (pruned outputs untouched)
        wait for 5 ns;
        clk <= '1'; wait for 5 ns; clk <= '0';
        wait for 1 ns;
        assert sib_comb = (x"11" xor x"55") report "cycle1 sib_comb";
        assert sib_ff   = (x"11" + x"02")   report "cycle1 sib_ff";

        -- Trigger force on pruned outputs
        go_force <= true;
        wait for 5 ns;

        -- Cycle 2 with din = 0x22 while pruned outputs are forced
        din <= x"22";
        wait for 5 ns;
        clk <= '1'; wait for 5 ns; clk <= '0';
        assert sib_comb = (x"22" xor x"55") report "cycle2 sib_comb";
        assert sib_ff   = (x"22" + x"02")   report "cycle2 sib_ff";

        -- Wait for release, then return din to 0x11 and clock (tests LAST_OUT_VALID!)
        wait for 5 ns;
        din <= x"11";
        wait for 5 ns;
        clk <= '1'; wait for 5 ns; clk <= '0';
        assert sib_comb = (x"11" xor x"55") report "cycle3 sib_comb";
        assert sib_ff   = (x"11" + x"02")   report "cycle3 sib_ff";

        wait until done;
        wait;
    end process;
end architecture;
