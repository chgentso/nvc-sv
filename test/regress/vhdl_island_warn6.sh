set -xe

# With --ieee-warnings=off for the whole run a combinational process whose
# calls can only report IEEE package warnings runs without its run guard;
# one with report statements keeps it, and so does every process with
# --ieee-warnings=off-at-0, which turns the warnings on after time 0.  The
# output must be identical to --rtl-islands=off in each case.

cat >warn6.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity warn6_dut is
    port (
        clk : in  std_logic;
        q   : out std_logic_vector(1 downto 0) );
end entity;

architecture rtl of warn6_dut is
    signal v  : unsigned(7 downto 0) := X"00";
    signal x  : unsigned(3 downto 0);   -- Never driven: all 'U'
    signal y0 : std_logic;
    signal y1 : natural := 0;
begin
    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            v <= v + 1;
        end if;
    end process;

    -- Only an event on v(5 downto 2) runs it
    c_warn : y0 <= '1' when v(5 downto 2) < x else '0';

    -- Reports on every run, whatever --ieee-warnings says
    p_rep : process (v) is
    begin
        y1 <= to_integer(v(7 downto 5));
        report "p_rep " & integer'image(to_integer(v(7 downto 5)));
    end process;

    q <= y0 & std_logic(to_unsigned(y1, 1)(0));
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity warn6_tb is
end entity;

architecture test of warn6_tb is
    signal clk : std_logic := '0';
    signal q   : std_logic_vector(1 downto 0);
begin
    dut : entity work.warn6_dut port map (clk, q);

    clk <= not clk after 5 ns when now < 400 ns;
end architecture;
VHDL_EOF

nvc --std=2008 --work=w_off -a warn6.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e warn6_tb

nvc --std=2008 --work=w_b -a warn6.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_b --rtl-islands=hier -e warn6_tb \
    > b_dump.log
cat b_dump.log
grep -q 'WARN6_TB.DUT islands=1 fused_procs=4/4' b_dump.log

for w in on off off-at-0; do
    nvc --std=2008 --work=w_off -r --ieee-warnings=$w warn6_tb > off_$w.out 2>&1
    nvc --std=2008 --work=w_b -r --ieee-warnings=$w warn6_tb > b_$w.out 2>&1
    nvc --std=2008 --work=w_b -r --ieee-warnings=$w --no-island-fast \
        warn6_tb > bnf_$w.out 2>&1
    NVC_ISLAND_ACT=0 nvc --std=2008 --work=w_b -r --ieee-warnings=$w \
        warn6_tb > bna_$w.out 2>&1
done

test $(grep -c 'metavalue detected' off_on.out) -gt 2
test $(grep -c 'metavalue detected' off_off-at-0.out) -gt 2
test $(grep -c 'metavalue detected' off_off.out) -eq 0
test $(grep -c 'p_rep' off_off.out) -gt 10

sed -i '/^\*\* Debug: /d' *.out   # debug builds only
for w in on off off-at-0; do
    for m in b bnf bna; do
        diff -u off_$w.out ${m}_$w.out
    done
done
