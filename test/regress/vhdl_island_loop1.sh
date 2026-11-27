set -xe

# Combinational processes that form a loop through signals (two processes
# counting towards an input, a ring of three processes) are fused into one
# RTL island.  A process that runs after the writer of a signal it reads
# must still see the value of the current delta cycle, so every value
# change happens in the same delta cycle as with --rtl-islands=off.

cat >loop1.vhd <<'VHDL_EOF'
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
    signal u, uv : unsigned(3 downto 0) := (others => '0');
    signal lp : std_logic_vector(3 downto 0) := (others => '0');
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

    -- A loop between two processes: u and uv climb one step every two
    -- delta cycles until they reach the input value
    p_loop1 : process (all) is
    begin
        if uv < unsigned(q) then
            u <= uv + 1;
        elsif uv > unsigned(q) then
            u <= uv - 1;
        end if;
    end process;

    p_loop2 : process (u) is
    begin
        uv <= u;
    end process;

    -- Three processes in a ring with a glitch
    p_r1 : process (all) is
    begin
        lp(0) <= q(2) or lp(2);
    end process;

    p_r2 : process (all) is
    begin
        lp(1) <= lp(0);
    end process;

    p_r3 : process (all) is
    begin
        lp(2) <= lp(1);
        lp(3) <= lp(0) xor lp(1);
    end process;

    dout <= c xor std_logic_vector(uv) xor lp;
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

nvc --std=2008 --work=w_off -a loop1.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e sr_tb \
    -r --wave=off.vcd --format=vcd > off.log 2>&1
grep -q "done" off.log

nvc --std=2008 --work=w_b -a loop1.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_b --rtl-islands=hier -e sr_tb \
    > b_dump.log
cat b_dump.log
grep -q 'SR_TB.DUT islands=1 fused_procs=14/14' b_dump.log

nvc --std=2008 --work=w_b -r --wave=b.vcd --format=vcd sr_tb > b.log 2>&1
nvc --std=2008 --work=w_b -r --no-island-fast sr_tb > bnf.log 2>&1
NVC_ISLAND_ACT=0 nvc --std=2008 --work=w_b -r sr_tb > b0.log 2>&1
NVC_ISLAND_ACT_CHECK=1 nvc --std=2008 --work=w_b -r sr_tb > bchk.log 2>&1

nvc --std=2008 --work=w_s -a loop1.vhd
NVC_ISLAND_SPEC=1 nvc --std=2008 --work=w_s --rtl-islands=hier -e sr_tb
NVC_ISLAND_SPEC_FORCE=A nvc --std=2008 --work=w_s -r sr_tb > s.log 2>&1

nvc --std=2008 --work=w_a -a loop1.vhd
nvc --std=2008 --work=w_a --rtl-islands=hier --2state=islands -e sr_tb
nvc --std=2008 --work=w_a -r sr_tb > a.log 2>&1

for m in b bnf b0 bchk s a; do
    sed -i '/^\*\* Debug: /d' *.log   # debug builds only: JIT code footprint etc.
    diff -u off.log $m.log
done
sed '/^\$date/,/\$end/d' off.vcd > off.vcd.s
sed '/^\$date/,/\$end/d' b.vcd > b.vcd.s
cmp off.vcd.s b.vcd.s
