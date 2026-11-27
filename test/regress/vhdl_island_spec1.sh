set -xe

# Speculative Mode A (NVC_ISLAND_SPEC=1): each Mode B island is also lowered
# as packed two-state.  NVC_ISLAND_SPEC_FORCE=A runs that variant whenever
# the island state is all '0'/'1'.  A design whose values stay 0/1 must give
# the same output and waveform as Mode B and --rtl-islands=off, with every
# call of the island in Mode A.

cat >spec1.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity spec1_dut is
    port (
        clk  : in  std_logic;
        rst  : in  std_logic;
        din  : in  std_logic_vector(7 downto 0);
        sel  : in  unsigned(1 downto 0);
        dout : out std_logic_vector(7 downto 0);
        sum  : out unsigned(11 downto 0);
        flag : out std_logic );
end entity;

architecture rtl of spec1_dut is
    type mem_t is array (0 to 3) of std_logic_vector(7 downto 0);
    type rom_t is array (0 to 3) of std_logic_vector(7 downto 0);
    constant ROM : rom_t := (x"3c", x"a5", x"0f", x"f0");
    type state_t is (IDLE, RUN, DONE);
    signal mem   : mem_t := (others => (others => '0'));
    signal acc   : unsigned(11 downto 0) := (others => '0');
    signal state : state_t := IDLE;
    signal cnt   : integer range 0 to 15 := 0;
    signal mix   : std_logic_vector(7 downto 0) := (others => '0');
    signal par   : std_logic := '0';
begin
    -- Combinational: ROM lookup, case on a vector, integer conversion
    p_mix : process (din, sel, mem) is
        variable v : std_logic_vector(7 downto 0);
    begin
        v := din xor ROM(to_integer(sel));
        case sel is
            when "00"   => v := v and mem(0);
            when "01"   => v := v or mem(1);
            when "ZZ"   => v := (others => '1');   -- never with 0/1 sel
            when others => v := not v;
        end case;
        mix <= v;
    end process;

    par <= mix(0) xor mix(3) xor mix(7);

    p_ff : process (clk, rst) is
    begin
        if rst = '1' then
            acc <= (others => '0');
            state <= IDLE;
            cnt <= 0;
            mem <= (others => (others => '0'));
        elsif rising_edge(clk) then
            mem(to_integer(sel)) <= mix;
            acc <= acc + unsigned(mix) + to_unsigned(cnt, 12);
            case state is
                when IDLE => state <= RUN;
                when RUN =>
                    if cnt = 9 then
                        state <= DONE;
                    else
                        cnt <= cnt + 1;
                    end if;
                when DONE => state <= IDLE; cnt <= 0;
            end case;
        end if;
    end process;

    dout <= mix;
    sum  <= acc;
    flag <= par when state = RUN else '0';
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity spec1_tb is end entity;

architecture test of spec1_tb is
    signal clk  : std_logic;   -- 'U' in the island state for the whole run:
                               -- the clock-only copy is never updated
    signal rst  : std_logic := '1';
    signal din  : std_logic_vector(7 downto 0) := x"00";
    signal sel  : unsigned(1 downto 0) := "00";
    signal dout : std_logic_vector(7 downto 0);
    signal sum  : unsigned(11 downto 0);
    signal flag : std_logic;
begin
    dut : entity work.spec1_dut
        port map (clk, rst, din, sel, dout, sum, flag);

    stim : process is
    begin
        clk <= '0';
        wait for 3 ns;
        rst <= '0';
        for k in 0 to 40 loop
            din <= std_logic_vector(to_unsigned((k * 37 + 11) mod 256, 8));
            sel <= to_unsigned(k mod 4, 2);
            clk <= '1';
            wait for 5 ns;
            clk <= '0';
            wait for 5 ns;
            report integer'image(k) & " " & to_hstring(dout) & " "
                & to_hstring(sum) & " " & std_logic'image(flag);
        end loop;
        wait;
    end process;
end architecture;
VHDL_EOF

unset NVC_ISLAND_SPEC NVC_ISLAND_SPEC_FORCE NVC_ISLAND_SPEC_STATS

nvc --std=2008 --work=w_off -a spec1.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e spec1_tb -r --wave=off.vcd \
    2>&1 | grep -v '^\*\* Debug' > off.log

nvc --std=2008 --work=w_b -a spec1.vhd
nvc --std=2008 --work=w_b --rtl-islands=hier -e spec1_tb -r --wave=b.vcd \
    2>&1 | grep -v '^\*\* Debug' > b.log

export NVC_ISLAND_SPEC=1
nvc --std=2008 --work=w_s -a spec1.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_s --rtl-islands=hier -e spec1_tb \
    > s_dump.log
NVC_ISLAND_SPEC_FORCE=A NVC_ISLAND_SPEC_STATS=1 \
    nvc --std=2008 --work=w_s -r --wave=s.vcd spec1_tb 2>&1 \
    | grep -v '^\*\* Debug' > s_raw.log
grep -v '^ISLAND_SPEC:' s_raw.log > s.log

cat s_dump.log s_raw.log
grep -q '^\*\* Note: .* 40 ' off.log
diff -u off.log b.log
diff -u off.log s.log
sed '/^\$date/,/\$end/d' b.vcd > b_nd.vcd
sed '/^\$date/,/\$end/d' s.vcd > s_nd.vcd
diff -u b_nd.vcd s_nd.vcd

# Every island of the DUT has a Mode A variant.  Only the first calls see
# 'U' (the outputs before their first evaluation); after that the state is
# 0/1-clean and all calls run in Mode A without a deopt stub firing.
grep -q 'SPEC1_TB.DUT island=[0-9]* procs=[0-9]* mode=B a_variant=yes' \
     s_dump.log
! grep -q 'SPEC1_TB.DUT .*a_variant=no' s_dump.log
grep -q '^ISLAND_SPEC: islands=[1-9][0-9]* a_variant=[1-9]' s_raw.log
grep -E '^ISLAND_SPEC: island=' s_raw.log > calls.log
! grep -q 'calls_a=0 ' calls.log
grep -q 'literal=0 range=0 conv=0 report=0 state=[0-9] *$' calls.log
