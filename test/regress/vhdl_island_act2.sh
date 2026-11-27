set -xe

# Combinational processes of an RTL island only run when a signal they read
# changed.  Every value change must happen in the same delta cycle as with
# --rtl-islands=off, in Mode B and Mode A, with --no-island-fast and as with
# the activity test disabled (NVC_ISLAND_ACT=0): a process reading a memory
# (a sparse RAM in Mode B, its readers always run), a latch, a vector
# written by two processes and the chains of vhdl_island_act1.

cat >act2.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity act2_dut is
    port (
        clk   : in  std_logic;
        en    : in  std_logic;
        addr  : in  unsigned(6 downto 0);
        din   : in  std_logic_vector(15 downto 0);
        rdata : out std_logic_vector(15 downto 0);
        lat   : out std_logic_vector(15 downto 0);
        split : out std_logic_vector(15 downto 0) );
end entity;

architecture rtl of act2_dut is
    type mem_t is array (0 to 127) of std_logic_vector(7 downto 0);
    signal mem : mem_t := (others => (others => '0'));
    signal rd, lt, sp : std_logic_vector(15 downto 0) := (others => '0');
    signal dq : std_logic_vector(15 downto 0) := (others => '0');
begin
    p_wr : process (clk) is
    begin
        if rising_edge(clk) then
            mem(to_integer(addr)) <= din(7 downto 0);
            dq <= din;
        end if;
    end process;

    -- Reads the memory
    p_rd : process (all) is
    begin
        rd <= dq(15 downto 8) & mem(to_integer(addr));
    end process;

    -- Latch: only assigns while en is high
    p_lat : process (all) is
    begin
        if en = '1' then
            lt <= rd xor dq;
        end if;
    end process;

    -- Two processes write disjoint halves of one vector
    p_lo : process (all) is
    begin
        sp(7 downto 0) <= lt(7 downto 0);
    end process;

    p_hi : process (all) is
    begin
        sp(15 downto 8) <= std_logic_vector(unsigned(rd(15 downto 8)) + 1);
    end process;

    rdata <= rd;
    lat   <= lt;
    split <= sp;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity act2_tb is
end entity;

architecture test of act2_tb is
    signal clk, en : std_logic := '0';
    signal addr    : unsigned(6 downto 0) := (others => '0');
    signal din     : std_logic_vector(15 downto 0) := (others => '0');
    signal rdata, lat, split : std_logic_vector(15 downto 0);
begin
    dut : entity work.act2_dut
        port map (clk, en, addr, din, rdata, lat, split);

    p_log : process (rdata, lat, split) is
    begin
        if rdata'event then report "rdata=" & to_hstring(rdata); end if;
        if lat'event then report "lat=" & to_hstring(lat); end if;
        if split'event then report "split=" & to_hstring(split); end if;
    end process;

    p_stim : process is
        variable v : unsigned(15 downto 0) := x"1234";
    begin
        for k in 0 to 15 loop
            addr <= to_unsigned((k * 5) mod 8, 7);
            din <= std_logic_vector(v);
            en <= '1' when k mod 3 /= 1 else '0';
            v := v + x"0f1d";
            wait for 1 ns;
            clk <= '1'; wait for 1 ns; clk <= '0'; wait for 1 ns;
        end loop;
        report "done";
        wait;
    end process;
end architecture;
VHDL_EOF

for t in act2_tb vhdl_island_act1; do
    case $t in
        act2_tb) src=act2.vhd ;;
        *) src=$TESTDIR/regress/vhdl_island_act1.vhd ;;
    esac

    nvc --std=2008 --work=w_off -a $src
    nvc --std=2008 --work=w_off --rtl-islands=off -e $t -r > $t.off.log 2>&1

    nvc --std=2008 --work=w_b -a $src
    NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_b --rtl-islands=hier -e $t \
        > $t.b_dump.log
    nvc --std=2008 --work=w_b -r $t > $t.b.log 2>&1
    NVC_ISLAND_ACT=0 nvc --std=2008 --work=w_b -r $t > $t.b0.log 2>&1
    nvc --std=2008 --work=w_b -r --no-island-fast $t > $t.bnf.log 2>&1

    nvc --std=2008 --work=w_a -a $src
    NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_a --rtl-islands=hier \
        --2state=islands -e $t > $t.a_dump.log
    nvc --std=2008 --work=w_a -r $t > $t.a.log 2>&1
    NVC_ISLAND_ACT=0 nvc --std=2008 --work=w_a -r $t > $t.a0.log 2>&1

    cat $t.b_dump.log $t.a_dump.log
    grep -q "done" $t.off.log
    for m in b b0 bnf; do
        diff -u $t.off.log $t.$m.log
    done
    diff -u $t.a0.log $t.a.log
done

# vhdl_island_act1 starts from 'X' and forces a signal, which Mode A cannot
# represent the same way
diff -u act2_tb.off.log act2_tb.a.log

grep -q 'ACT2_TB.DUT islands=1 fused_procs=8/8' act2_tb.b_dump.log
grep -q 'ACT2_TB.DUT islands=1 fused_procs=8/8' act2_tb.a_dump.log
grep -q 'ACT1.DUT islands=1 fused_procs=10/12' vhdl_island_act1.a_dump.log
