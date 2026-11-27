set -xe

# A combinational process that reads a signal it also assigns sees the
# value of the current delta cycle and runs again in the next one when the
# signal changed.  Fused into an RTL island, every value change must happen
# in the same delta cycle as with --rtl-islands=off: in Mode B, with
# --no-island-fast, without the activity test (NVC_ISLAND_ACT=0), with the
# activity checker and in the speculative Mode A variant.

cat >selfread1.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity sr_dut is
    port (
        clk  : in  std_logic;
        din  : in  std_logic_vector(3 downto 0);
        dout : out std_logic_vector(3 downto 0);
        sh   : out std_logic_vector(3 downto 0);
        cnt  : out unsigned(3 downto 0);
        flag : out std_logic );
end entity;

architecture rtl of sr_dut is
    type rec_t is record
        x : std_logic;
        y : std_logic_vector(3 downto 0);
    end record;
    signal q, a, b, c : std_logic_vector(3 downto 0) := (others => '0');
    signal v, w : std_logic_vector(3 downto 0) := (others => '0');
    signal r : rec_t := ('0', "0000");
    signal n : unsigned(3 downto 0) := (others => '0');
    signal f, g : std_logic := '0';
begin
    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            q <= din;
        end if;
    end process;

    -- Reads a and b after assigning them: sees the values of the current
    -- delta, so b and c follow q one and two deltas later
    p_chain : process (all) is
    begin
        a <= q;
        b <= a;
        c <= b xor a;
    end process;

    -- Shift register over delta cycles: settles after four deltas
    p_shift : process (q, v) is
    begin
        v <= v(2 downto 0) & q(0);
    end process;

    -- Record fields, read-before-write and read-after-write mixed
    p_rec : process (all) is
        variable t : std_logic;
    begin
        t := r.x;
        r.x <= q(1);
        r.y <= r.y(2 downto 0) & t;
        w <= r.y;
    end process;

    -- Counts up in delta cycles until it reaches the input value
    p_count : process (all) is
    begin
        if n < unsigned(q) then
            n <= n + 1;
        elsif n > unsigned(q) then
            n <= n - 1;
        end if;
        g <= q(3) xor q(2);
        f <= g;
    end process;

    dout <= c;
    sh   <= v or w;
    cnt  <= n;
    flag <= f;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity sr_tb is
end entity;

architecture test of sr_tb is
    signal clk  : std_logic := '0';
    signal din  : std_logic_vector(3 downto 0) := (others => '0');
    signal dout, sh : std_logic_vector(3 downto 0);
    signal cnt  : unsigned(3 downto 0);
    signal flag : std_logic;
begin
    dut : entity work.sr_dut port map (clk, din, dout, sh, cnt, flag);

    p_log : process (dout, sh, cnt, flag) is
    begin
        if dout'event then report "dout=" & to_string(dout); end if;
        if sh'event then report "sh=" & to_string(sh); end if;
        if cnt'event then report "cnt=" & to_string(to_integer(cnt)); end if;
        if flag'event then report "flag=" & to_string(flag); end if;
    end process;

    p_stim : process is
        constant vals : std_logic_vector(31 downto 0) := x"5a3f0c96";
    begin
        for k in 0 to 7 loop
            din <= vals(4 * k + 3 downto 4 * k);
            wait for 1 ns;
            clk <= '1'; wait for 1 ns; clk <= '0'; wait for 1 ns;
        end loop;
        report "done";
        wait;
    end process;
end architecture;
VHDL_EOF

nvc --std=2008 --work=w_off -a selfread1.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e sr_tb \
    -r --wave=off.vcd --format=vcd > off.log 2>&1
grep -q "done" off.log

nvc --std=2008 --work=w_b -a selfread1.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_b --rtl-islands=hier -e sr_tb \
    > b_dump.log
cat b_dump.log
grep -q 'SR_TB.DUT islands=1 fused_procs=9/9' b_dump.log

nvc --std=2008 --work=w_b -r --wave=b.vcd --format=vcd sr_tb > b.log 2>&1
nvc --std=2008 --work=w_b -r --no-island-fast sr_tb > bnf.log 2>&1
NVC_ISLAND_ACT=0 nvc --std=2008 --work=w_b -r sr_tb > b0.log 2>&1
NVC_ISLAND_ACT_CHECK=1 nvc --std=2008 --work=w_b -r sr_tb > bchk.log 2>&1

nvc --std=2008 --work=w_s -a selfread1.vhd
NVC_ISLAND_SPEC=1 NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_s \
    --rtl-islands=hier -e sr_tb > s_dump.log
grep -q 'SR_TB.DUT island=0 procs=9 mode=B a_variant=yes' s_dump.log
NVC_ISLAND_SPEC_FORCE=A nvc --std=2008 --work=w_s -r sr_tb > s.log 2>&1

# Debug builds print the size of the code compiled so far, which
# depends on how far the background compilation got
sed -i '/JIT code footprint/d' *.log
for m in b bnf b0 bchk s; do
    diff -u off.log $m.log
done
sed '/^\$date/,/\$end/d' off.vcd > off.vcd.s
sed '/^\$date/,/\$end/d' b.vcd > b.vcd.s
cmp off.vcd.s b.vcd.s
