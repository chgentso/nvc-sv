set -xe

# The output of the processes of a delta cycle (reports, assertions, TEXTIO
# writes to OUTPUT, Verilog $display) is written in the order in which the
# processes start, so it is the same with RTL islands on or off: processes
# fused into an island run at one point of the cycle.  A failure that
# stops the simulation in a fused process keeps its place.

cat >rord1.v <<'SV_EOF'
module rord1_sv (input [3:0] y);
  event e;
  // Waits for the event first but must print after what triggered it
  always @(e) $display("sv got event y=%h", y);

  always @(y) begin
    $display("sv sees y=%h", y);
    -> e;
    $display("sv triggered y=%h", y);
  end
endmodule
SV_EOF

cat >rord1.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity rord1_dut is
    port ( clk : in  std_logic;
           d   : in  unsigned(3 downto 0);
           y   : out unsigned(3 downto 0) );
end entity;

architecture rtl of rord1_dut is
    signal q, r : unsigned(3 downto 0) := (others => '0');
begin
    -- Combinational processes with reports, fused into an RTL island
    p_first : process (all) is
    begin
        r <= q + 1;
        report "first q=" & to_hstring(q);
    end process;

    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            q <= d;
        end if;
    end process;

    p_second : process (all) is
    begin
        y <= r;
        if r(0) = '1' then
            report "second r=" & to_hstring(r) severity warning;
        end if;
        assert r /= 10 report "second r is ten" severity failure;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;

entity rord1_tb is
end entity;

architecture test of rord1_tb is
    signal clk : std_logic := '0';
    signal d, y : unsigned(3 downto 0) := (others => '0');
begin
    -- Not fused: waits and writes with TEXTIO
    p_watch_q : process is
        variable l : line;
    begin
        wait on y;
        write(l, string'("watch y=") & to_hstring(y));
        writeline(output, l);
        report "watch reports y=" & to_hstring(y);
    end process;

    dut : entity work.rord1_dut port map (clk, d, y);

    sv : entity work.rord1_sv port map (y => std_logic_vector(y));

    p_watch_y : process (y) is
    begin
        report "y changed to " & to_hstring(y);
    end process;

    p_stim : process is
    begin
        for i in 1 to 3 loop
            d <= to_unsigned(i * 3, 4);
            wait for 1 ns;
            clk <= '1';
            wait for 1 ns;
            clk <= '0';
        end loop;
        report "done";
        wait;
    end process;
end architecture;
VHDL_EOF

cat >expect.log <<'LOG_EOF'
** Note: 0ms+0: first q=0
   Process :rord1_tb:dut:p_first at rord1.vhd:18
** Note: 0ms+0: y changed to X
   Process :rord1_tb:p_watch_y at rord1.vhd:66
watch y=0
** Note: 0ms+1: watch reports y=0
   Process :rord1_tb:p_watch_q at rord1.vhd:57
** Warning: 0ms+1: second r=1
   Process :rord1_tb:dut:p_second at rord1.vhd:32
sv sees y=0
sv triggered y=0
** Note: 0ms+1: y changed to 0
   Process :rord1_tb:p_watch_y at rord1.vhd:66
watch y=1
** Note: 0ms+2: watch reports y=1
   Process :rord1_tb:p_watch_q at rord1.vhd:57
sv got event y=1
** Note: 0ms+2: y changed to 1
   Process :rord1_tb:p_watch_y at rord1.vhd:66
sv sees y=1
sv triggered y=1
sv got event y=1
** Note: 1ns+2: first q=3
   Process :rord1_tb:dut:p_first at rord1.vhd:18
watch y=4
** Note: 1ns+4: watch reports y=4
   Process :rord1_tb:p_watch_q at rord1.vhd:57
sv sees y=4
sv triggered y=4
** Note: 1ns+4: y changed to 4
   Process :rord1_tb:p_watch_y at rord1.vhd:66
sv got event y=4
** Note: 3ns+2: first q=6
   Process :rord1_tb:dut:p_first at rord1.vhd:18
** Warning: 3ns+3: second r=7
   Process :rord1_tb:dut:p_second at rord1.vhd:32
watch y=7
** Note: 3ns+4: watch reports y=7
   Process :rord1_tb:p_watch_q at rord1.vhd:57
sv sees y=7
sv triggered y=7
** Note: 3ns+4: y changed to 7
   Process :rord1_tb:p_watch_y at rord1.vhd:66
sv got event y=7
** Note: 5ns+2: first q=9
   Process :rord1_tb:dut:p_first at rord1.vhd:18
** Failure: 5ns+3: second r is ten
   Process :rord1_tb:dut:p_second at rord1.vhd:34
LOG_EOF

# Without the messages a debug build adds at the end
nodebug() {
    grep -v '^\*\* Debug:' $1 > $1.tmp || true
    mv $1.tmp $1
}

nvc --std=2008 --work=w_off -a rord1.v rord1.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e rord1_tb
nvc --std=2008 --work=w_off -r rord1_tb > off.log 2>&1 || true
nodebug off.log
diff -u expect.log off.log

nvc --std=2008 --work=w_on -a rord1.v rord1.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_on -e rord1_tb > dump.log
cat dump.log
grep -q 'RORD1_TB.DUT islands=1 fused_procs=3/3' dump.log

nvc --std=2008 --work=w_on -r rord1_tb > on.log 2>&1 || true
nodebug on.log
diff -u expect.log on.log

nvc --std=2008 --work=w_on -r --no-island-fast rord1_tb > nofast.log 2>&1 || true
nodebug nofast.log
diff -u expect.log nofast.log

NVC_ISLAND_SPEC=1 nvc --std=2008 --work=w_spec -a rord1.v rord1.vhd
NVC_ISLAND_SPEC=1 nvc --std=2008 --work=w_spec -e rord1_tb
nvc --std=2008 --work=w_spec -r rord1_tb > spec.log 2>&1 || true
nodebug spec.log
diff -u expect.log spec.log

# Output as it happens: the same lines, perhaps in another order
nvc --std=2008 --work=w_on -r --report-order=immediate rord1_tb \
    > imm.log 2>&1 || true
nodebug imm.log
NVC_REPORT_ORDER=immediate nvc --std=2008 --work=w_on -r rord1_tb \
    > imm_env.log 2>&1 || true
nodebug imm_env.log
diff -u imm.log imm_env.log
diff -u <(sort expect.log) <(sort imm.log)

if nvc --std=2008 --work=w_on -r --report-order=sorted rord1_tb \
       > bad.log 2>&1; then
    exit 1
fi
grep -q "invalid --report-order option 'sorted'" bad.log
