set -xe

# Verify dynamic direct-link break (comb + FF) and super-domain force/release
# fallback across --island-fast, --no-island-fast, and NVC_RTL_ISLANDS=off.

cat >direct_break.vhd <<'EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity db_stage is
    generic (
        TAG : unsigned(7 downto 0) := x"01"
    );
    port (
        clk   : in  std_logic;
        din   : in  unsigned(7 downto 0);
        c_out : out unsigned(7 downto 0);
        q_out : out unsigned(7 downto 0)
    );
end entity;

architecture rtl of db_stage is
    signal q_reg : unsigned(7 downto 0) := TAG;
begin
    p_comb : process (din, q_reg) is
    begin
        c_out <= (din xor TAG) + q_reg;
    end process;

    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            q_reg <= din + TAG;
        end if;
    end process;

    q_out <= q_reg;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity direct_break_tb is
end entity;

architecture test of direct_break_tb is
    signal clk  : std_logic := '0';
    signal din  : unsigned(7 downto 0) := x"10";
    signal c0, q0 : unsigned(7 downto 0);
    signal c1, q1 : unsigned(7 downto 0);
    signal c2, q2 : unsigned(7 downto 0);
    signal phase  : integer := 0;
begin
    -- 3 chained islands sharing a single super-domain clock & direct comb+FF wires
    u0 : entity work.db_stage generic map (TAG => x"03")
        port map (clk => clk, din => din, c_out => c0, q_out => q0);
    u1 : entity work.db_stage generic map (TAG => x"05")
        port map (clk => clk, din => c0,  c_out => c1, q_out => q1);
    u2 : entity work.db_stage generic map (TAG => x"07")
        port map (clk => clk, din => c1,  c_out => c2, q_out => q2);

    p_force : process is
    begin
        wait until phase = 1;
        -- Break combinational direct link u0.c_out -> u1.din AND force u1.q_reg
        << signal u0.c_out : unsigned(7 downto 0) >> <= force x"40";
        << signal u1.q_reg : unsigned(7 downto 0) >> <= force x"20";
        wait for 1 ns;
        assert << signal u1.c_out : unsigned(7 downto 0) >> = ((x"40" xor x"05") + x"20")
            report "forced c1 mismatch";
        wait until phase = 2;
        assert << signal u1.c_out : unsigned(7 downto 0) >> = ((x"40" xor x"05") + x"20")
            report "forced c1 did not hold across clock";
        << signal u0.c_out : unsigned(7 downto 0) >> <= release;
        << signal u1.q_reg : unsigned(7 downto 0) >> <= release;
        wait;
    end process;

    p_stim : process is
        variable exp_q0, exp_c0, exp_q1, exp_c1, exp_q2, exp_c2 : unsigned(7 downto 0);
    begin
        -- Phase 0: normal super-domain + direct-comb propagation (2 cycles)
        wait for 5 ns;
        clk <= '1'; wait for 5 ns; clk <= '0';
        din <= x"18";
        wait for 5 ns;
        clk <= '1'; wait for 5 ns; clk <= '0';
        wait for 1 ns;

        -- Phase 1: force active (super-domain drops to slow path, direct_comb_descs refreshed)
        phase <= 1;
        wait for 4 ns;
        din <= x"22";
        wait for 5 ns;
        clk <= '1'; wait for 5 ns; clk <= '0';
        wait for 1 ns;

        -- Phase 2: release (n_forced_islands returns to 0; broken direct link must still work!)
        phase <= 2;
        wait for 4 ns;
        din <= x"33";
        wait for 5 ns;
        clk <= '1'; wait for 5 ns; clk <= '0';
        wait for 5 ns;
        clk <= '1'; wait for 5 ns; clk <= '0';
        wait for 5 ns;
        clk <= '1'; wait for 5 ns; clk <= '0';
        wait for 1 ns;
        exp_q0 := x"33" + x"03";
        exp_c0 := (x"33" xor x"03") + exp_q0;
        exp_q1 := exp_c0 + x"05";
        exp_c1 := (exp_c0 xor x"05") + exp_q1;
        exp_q2 := exp_c1 + x"07";
        exp_c2 := (exp_c1 xor x"07") + exp_q2;
        assert q2 = exp_q2 report "post-release q2 mismatch";
        assert c2 = exp_c2 report "post-release c2 mismatch";
        wait;
    end process;
end architecture;
EOF

strip_date() {
   sed '/^\$date/,/^\$end/d' "$1" > "$2"
}

NVC_RTL_ISLANDS=hier nvc --std=2008 -a direct_break.vhd -e direct_break_tb
NVC_RTL_ISLANDS=hier NVC_DUMP_ISLANDS=1 nvc --std=2008 -r --island-fast direct_break_tb >hier.log 2>&1
grep -q 'super_domains=[1-9]' hier.log
grep -q 'direct_wires=[1-9]' hier.log

NVC_RTL_ISLANDS=hier nvc --std=2008 -r --island-fast --wave=fast.vcd --format=vcd direct_break_tb
NVC_RTL_ISLANDS=hier nvc --std=2008 -r --no-island-fast --wave=nofast.vcd --format=vcd direct_break_tb
NVC_RTL_ISLANDS=off  nvc --std=2008 -a direct_break.vhd -e direct_break_tb
NVC_RTL_ISLANDS=off  nvc --std=2008 -r --no-island-fast --wave=off.vcd --format=vcd direct_break_tb

strip_date fast.vcd   fast.norm
strip_date nofast.vcd nofast.norm
strip_date off.vcd    off.norm
diff -u nofast.norm fast.norm
diff -u off.norm    fast.norm
