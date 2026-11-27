set -xe

# Speculative Mode A at run time (NVC_ISLAND_SPEC=1, no FORCE): islands run
# Mode B from time zero and switch to their Mode A variant, holding the
# signals that are not '0'/'1' as Mode B bytes.  Covered: promotion after
# reset; a register left 'U' that feeds comb logic and an output read by
# another island and by an unfused process, later written (its readers
# deopt until then); a register written for a while and read later; 'X',
# 'Z', 'H', 'L' and 'U' arriving on an input of a Mode A island (held, then
# packed again once clean); two clock domains with an asynchronous reset; a report
# in a clocked process (pinned to Mode B after a few deopts); forces; and
# a full waveform dump (islands still switch: the kernel signals the dump
# reads get the same values in both modes).  Output and
# waveforms must be those of Mode B and --rtl-islands=off, also with
# NVC_ISLAND_SPEC_STRESS (conversions on every delta).

cat >rt1.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- A register with no reset and no initial value stays 'U' until WEN; a
-- second one is written only while WEN2 and read from RD on
entity u_e is
    port ( clk, wen, wen2, rd : in std_logic;
           d    : in  std_logic_vector(7 downto 0);
           q    : out std_logic_vector(7 downto 0);
           y, m, s, ql : out std_logic_vector(7 downto 0) );
end entity;
architecture rtl of u_e is
    signal ru, rl : std_logic_vector(7 downto 0);
begin
    process (clk) is
    begin
        if rising_edge(clk) then
            if wen = '1' then
                ru <= d;
            end if;
            if wen2 = '1' then
                rl <= not d;
            end if;
        end if;
    end process;
    y  <= (ru and d) or (ru xor d);
    m  <= d when ru(0) = '1' else not d;
    s  <= std_logic_vector(unsigned(ru) + unsigned(d));
    ql <= rl when rd = '1' else x"00";
    q  <= ru;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- Reads the output of u_e (another island)
entity down_e is
    port ( clk : in std_logic; a : in std_logic_vector(7 downto 0);
           q : out std_logic_vector(7 downto 0); p : out std_logic );
end entity;
architecture rtl of down_e is
    signal r : std_logic_vector(7 downto 0) := x"00";
begin
    process (clk) is
    begin
        if rising_edge(clk) then
            r <= a xor x"0f";
        end if;
    end process;
    q <= r;
    p <= a(0) xor a(7);
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- An input that takes 'X', 'Z', 'H', 'L' for a few cycles
entity x_e is
    port ( clk, rst, xin : in std_logic; d : in std_logic_vector(7 downto 0);
           q : out std_logic_vector(7 downto 0); c : out std_logic );
end entity;
architecture rtl of x_e is
    signal r : std_logic_vector(7 downto 0) := x"00";
    signal t : std_logic;
begin
    t <= xin and d(1);
    process (clk, rst) is
    begin
        if rst = '1' then
            r <= x"00";
        elsif rising_edge(clk) then
            r <= r(6 downto 0) & t;
        end if;
    end process;
    q <= r;
    c <= t or d(2);
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- Two clock domains (both edges) and an asynchronous reset
entity md_e is
    port ( clk, rst : in std_logic; d : in std_logic_vector(7 downto 0);
           q : out std_logic_vector(7 downto 0);
           n : out unsigned(7 downto 0) );
end entity;
architecture rtl of md_e is
    signal rp : std_logic_vector(7 downto 0);
    signal rn : unsigned(7 downto 0);
begin
    process (clk, rst) is
    begin
        if rst = '1' then
            rp <= x"00";
        elsif rising_edge(clk) then
            rp <= d xor std_logic_vector(rn);
        end if;
    end process;
    process (clk, rst) is
    begin
        if rst = '1' then
            rn <= x"01";
        elsif falling_edge(clk) then
            rn <= rn + unsigned(rp(3 downto 0));
        end if;
    end process;
    q <= rp;
    n <= rn;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- A report every fourth edge
entity rep_e is
    port ( clk, rst : in std_logic; q : out unsigned(7 downto 0) );
end entity;
architecture rtl of rep_e is
    signal r : unsigned(7 downto 0);
begin
    process (clk, rst) is
    begin
        if rst = '1' then
            r <= x"00";
        elsif rising_edge(clk) then
            r <= r + 1;
            if r(1 downto 0) = "10" then
                report "rep " & to_hstring(r);
            end if;
        end if;
    end process;
    q <= r;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity rt1_tb is end entity;

architecture test of rt1_tb is
    signal clk, rst, wen, wen2, rd, xin : std_logic := '0';
    signal d : std_logic_vector(7 downto 0) := x"00";
    signal u_q, u_y, u_m, u_s, u_ql, dn_q, x_q, md_q : std_logic_vector(7 downto 0);
    signal md_n, rep_q : unsigned(7 downto 0);
    signal dn_p, x_c : std_logic;
    signal mon_u, mon_dn, mon_x, mon_md : std_logic_vector(7 downto 0);
begin
    u_u   : entity work.u_e port map (clk, wen, wen2, rd, d, u_q, u_y, u_m, u_s, u_ql);
    u_dn  : entity work.down_e port map (clk, u_q, dn_q, dn_p);
    u_x   : entity work.x_e port map (clk, rst, xin, d, x_q, x_c);
    u_md  : entity work.md_e port map (clk, rst, d, md_q, md_n);
    u_rep : entity work.rep_e port map (clk, rst, rep_q);

    -- Not fused (wait statement): what the VCD records
    mon : process is
    begin
        wait on u_q, u_y, dn_q, x_q, md_q;
        mon_u <= u_y;
        mon_dn <= dn_q;
        mon_x <= x_q;
        mon_md <= md_q;
    end process;

    stim : process is
        procedure tick is
        begin
            clk <= '1';
            wait for 5 ns;
            clk <= '0';
            wait for 5 ns;
        end procedure;
    begin
        rst <= '1';
        xin <= '1';
        wait for 3 ns;
        rst <= '0';
        for k in 0 to 59 loop
            d <= std_logic_vector(to_unsigned((k * 53 + 29) mod 256, 8));
            wen2 <= '1' when k >= 3 and k < 7 else '0';
            wen <= '1' when k = 30 or k = 31 or k = 45 else '0';
            rd <= '1' when k >= 20 else '0';
            case k is
                when 10 => xin <= 'X';
                when 11 => xin <= 'Z';
                when 12 => xin <= 'H';
                when 13 => xin <= 'L';
                when 14 => xin <= '1';
                when 40 => xin <= 'U';
                when 41 => xin <= '0';
                when others => null;
            end case;
            if k = 24 then
                md_q <= force x"5a";
            elsif k = 27 then
                md_q <= release;
            elsif k = 34 then
                << signal .rt1_tb.u_md.rn : unsigned(7 downto 0) >> <= force x"c3";
            elsif k = 36 then
                << signal .rt1_tb.u_md.rn : unsigned(7 downto 0) >> <= release;
            end if;
            tick;
            report integer'image(k) & " u=" & to_string(u_q) & " y="
                & to_string(u_y) & " m=" & to_string(u_m) & " s="
                & to_string(u_s) & " ql=" & to_string(u_ql) & " dn="
                & to_string(dn_q) & " " & std_logic'image(dn_p) & " x="
                & to_string(x_q) & " " & std_logic'image(x_c) & " md="
                & to_hstring(md_q) & " " & to_hstring(md_n) & " rep="
                & to_hstring(rep_q);
        end loop;
        wait;
    end process;
end architecture;
VHDL_EOF

unset NVC_ISLAND_SPEC NVC_ISLAND_SPEC_FORCE NVC_ISLAND_SPEC_STATS
unset NVC_ISLAND_SPEC_STRESS NVC_ISLAND_SPEC_RT

nd() { sed '/^\$date/,/\$end/d' $1; }
mon=--include=':rt1_tb:mon_*'

nvc --std=2008 --work=w_off -a rt1.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e rt1_tb \
    -r --wave=off.vcd --format=vcd $mon 2>&1 | grep -v '^\*\* Debug' > off.log
nvc --std=2008 --work=w_off -r --wave=offfull.vcd --format=vcd rt1_tb \
    > /dev/null 2>&1
nvc --std=2008 --work=w_b -a rt1.vhd
nvc --std=2008 --work=w_b --rtl-islands=hier -e rt1_tb \
    -r --wave=b.vcd --format=vcd $mon 2>&1 | grep -v '^\*\* Debug' > b.log
diff -u off.log b.log
diff -u <(nd off.vcd) <(nd b.vcd)

export NVC_ISLAND_SPEC=1
nvc --std=2008 --work=w_s -a rt1.vhd
nvc --std=2008 --work=w_s --rtl-islands=hier -e rt1_tb
for st in 0 1 2 5; do
    NVC_ISLAND_SPEC_STRESS=$st NVC_ISLAND_SPEC_STATS=1 \
        nvc --std=2008 --work=w_s -r --wave=s$st.vcd --format=vcd $mon \
        rt1_tb 2>&1 | grep -v '^\*\* Debug' > s${st}_raw.log
    grep -v '^ISLAND_SPEC' s${st}_raw.log > s$st.log
    diff -u off.log s$st.log
    diff -u <(nd off.vcd) <(nd s$st.vcd)
done
NVC_ISLAND_SPEC_STATS=1 nvc --std=2008 --work=w_s -r --no-island-fast \
    rt1_tb 2>&1 | grep -v '^\*\* Debug' > nf_raw.log
diff -u off.log <(grep -v '^ISLAND_SPEC' nf_raw.log)
NVC_ISLAND_SPEC_STATS=1 nvc --std=2008 --work=w_s -r --wave=full.vcd \
    --format=vcd rt1_tb 2>&1 | grep -v '^\*\* Debug' > full_raw.log
diff -u off.log <(grep -v '^ISLAND_SPEC' full_raw.log)
diff -u <(nd offfull.vcd) <(nd full.vcd)

cat s0_raw.log full_raw.log | grep ISLAND_SPEC
# The 'U' register reaches comb logic, the next island and the monitor,
# then a write clears it
grep -q ' 0 u=UUUUUUUU y=UUUUUUUU .* s=XXXXXXXX .* dn=UUUUUUUU ' off.log
grep -q ' 31 u=10001000 y=10001000 ' off.log
# Every island was promoted, also with all signals dumped; the 'X' input
# is held as Mode B bytes, then packed again; the report island was pinned
grep -q '^ISLAND_SPEC_RT: ticks=[0-9]* variants=5 promoted=5 ' s0_raw.log
grep -q '^ISLAND_SPEC_RT: deopts .* report=[1-9][0-9]* ' s0_raw.log
grep -q '^ISLAND_SPEC_RT: held .* holds=[1-9][0-9]* unholds=[1-9]' s0_raw.log
grep -q '^ISLAND_SPEC_RT: pinned .* report=1 ' s0_raw.log
grep -q '^ISLAND_SPEC_RT: deopts .*stress=[1-9]' s1_raw.log
grep -q '^ISLAND_SPEC_RT: ticks=[0-9]* variants=5 promoted=5 ' full_raw.log
