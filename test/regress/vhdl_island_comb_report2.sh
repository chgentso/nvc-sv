set -xe

# A combinational process with report or assert statements fused into an
# RTL island must run exactly when it runs unfused: on its first call and
# when a signal of its sensitivity list changed (whole signal, record field
# or a constant element), not when the island runs for another reason or a
# signal gets a transaction without an event.  Each report must fire in the
# same delta cycle with the same text as with --rtl-islands=off; the order
# of reports from different processes within one delta cycle may differ.

cat >report2.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity rep_dut is
    port (
        clk  : in  std_logic;
        tick : in  std_logic;
        din  : in  unsigned(3 downto 0);
        dout : out unsigned(3 downto 0) );
end entity;

architecture rtl of rep_dut is
    signal q, n, m : unsigned(3 downto 0) := (others => '0');
    signal same : unsigned(3 downto 0) := (others => '0');
    type pair_t is record
        lo : std_logic_vector(1 downto 0);
        hi : unsigned(3 downto 0);
    end record;
    signal pr : pair_t := ("00", "0000");
    signal bits : std_logic_vector(1 downto 0) := "00";
begin
    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            q <= din;
            same <= din and "0000";   -- Assigned on every edge, never changes
        end if;
    end process;

    -- Counts towards q one delta at a time and reports every step
    p_count : process (all) is
    begin
        if n < q then
            n <= n + 1;
            report "count up from " & to_string(to_integer(n));
        elsif n > q then
            n <= n - 1;
            report "count down from " & to_string(to_integer(n));
        end if;
        assert n /= 9 report "n is nine" severity warning;
    end process;

    -- Sensitive to tick, which it does not read
    p_tick : process (tick, n, same) is
    begin
        m <= n + same;
        report "tick process ran, n=" & to_string(to_integer(n));
    end process;

    -- Sensitive to one bit and one record field only
    p_bit : process (q(1), pr.hi) is
    begin
        bits <= q(1) & pr.hi(0);
        report "bit process ran, q(1)=" & to_string(q(1));
    end process;

    p_pair : process (all) is
    begin
        pr.lo <= std_logic_vector(q(3 downto 2));
        pr.hi <= n;
        if pr.lo(1) = '1' then
            report "pair high, hi=" & to_string(to_integer(pr.hi));
        end if;
    end process;

    -- Reads the output of the reporting processes
    p_out : process (all) is
    begin
        dout <= m xor q xor ("00" & unsigned(bits));
        if m = q then
            report "settled at " & to_hstring(m) severity note;
        end if;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity rep_tb is
end entity;

architecture test of rep_tb is
    signal clk, tick : std_logic := '0';
    signal din  : unsigned(3 downto 0) := (others => '0');
    signal dout : unsigned(3 downto 0);
begin
    dut : entity work.rep_dut port map (clk, tick, din, dout);

    p_log : process (dout) is
    begin
        report "dout=" & to_hstring(dout);
    end process;

    p_stim : process is
        type int_vec is array (natural range <>) of natural;
        constant vals : int_vec := (3, 3, 10, 7, 0, 12);
    begin
        for k in vals'range loop
            din <= to_unsigned(vals(k), 4);
            wait for 1 ns;
            clk <= '1'; wait for 1 ns; clk <= '0';
            tick <= not tick;
            wait for 1 ns;
            tick <= tick;      -- A transaction without an event
            wait for 1 ns;
        end loop;
        report "done";
        wait;
    end process;
end architecture;
VHDL_EOF

# One line per report (time+delta, text, process), sorted
canon() {
    awk '/^\*\* /{ if (r != "") print r; r = $0; next }
         { r = r " |" $0 }
         END { if (r != "") print r }' $1 | sort
}

nvc --std=2008 --work=w_off -a report2.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e rep_tb -r > off.log 2>&1
grep -q "done" off.log
canon off.log > off.canon

nvc --std=2008 --work=w_b -a report2.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_b --rtl-islands=hier -e rep_tb \
    > b_dump.log
cat b_dump.log
grep -q 'REP_TB.DUT islands=1 fused_procs=6/6' b_dump.log

nvc --std=2008 --work=w_b -r rep_tb > b.log 2>&1
nvc --std=2008 --work=w_b -r --no-island-fast rep_tb > bnf.log 2>&1
NVC_ISLAND_ACT=0 nvc --std=2008 --work=w_b -r rep_tb > b0.log 2>&1
NVC_ISLAND_ACT_CHECK=1 nvc --std=2008 --work=w_b -r rep_tb > bchk.log 2>&1

for m in b bnf b0 bchk; do
    canon $m.log > $m.canon
    diff -u off.canon $m.canon
done
