set -xe

# Speculative Mode A clean tracking (NVC_ISLAND_SPEC=1): an island is
# promoted although some of its registers hold 'U'; they are held as Mode B
# bytes and the Mode A functions deopt only when they load or store one, so
# the island re-runs that delta in Mode B and the metavalue propagates
# exactly.  Covered: a register never written and read only for a few
# cycles (its 'U' reaches an output port, another island, an unfused
# process, TO_INTEGER warnings and the VCD); a register never written nor
# read (its island promotes once and never deopts); a register written one
# nibble at a time (still held after the first write, clean after the
# second); a register written late; an input that arrives 'X' or 'U' while
# its island is Mode A (held, packed again once clean).  Output and VCD must
# be those of Mode B and --rtl-islands=off, also with
# NVC_ISLAND_SPEC_STRESS and NVC_ISLAND_ACT_CHECK.

cat >held.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- RU is never written and read only while SEL; RN is never written nor
-- read; RP is written one nibble at a time; UIN starts 'U'
entity h_e is
    port ( clk, rst, sel, wlo, whi : in std_logic;
           d    : in  std_logic_vector(7 downto 0);
           uin  : in  std_logic_vector(3 downto 0);
           y, q : out std_logic_vector(7 downto 0);
           n    : out natural range 0 to 255;
           c    : out std_logic );
end entity;
architecture rtl of h_e is
    signal cnt : unsigned(7 downto 0);
    signal ru, rn, rp : std_logic_vector(7 downto 0);
begin
    process (clk, rst) is
    begin
        if rst = '1' then
            cnt <= (others => '0');
        elsif rising_edge(clk) then
            cnt <= cnt + 1;
            if wlo = '1' then
                rp(3 downto 0) <= d(3 downto 0);
            end if;
            if whi = '1' then
                rp(7 downto 4) <= d(7 downto 4);
            end if;
            if cnt = 250 then
                rn <= d;
            end if;
        end if;
    end process;
    y <= std_logic_vector(cnt) when sel = '0' else ru xor d;
    q <= rp;
    n <= to_integer(cnt) when sel = '0' else to_integer(unsigned(ru));
    c <= cnt(0) when sel = '0' else uin(0) xor uin(3);
end architecture;

library ieee;
use ieee.std_logic_1164.all;

-- Another island reading the outputs of h_e
entity hd_e is
    port ( clk : in std_logic; a, b : in std_logic_vector(7 downto 0);
           q : out std_logic_vector(7 downto 0) );
end entity;
architecture rtl of hd_e is
begin
    process (clk) is
    begin
        if rising_edge(clk) then
            q <= a xor b;
        end if;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- RX is never written nor read: the island promotes once and stays Mode A
entity nr_e is
    port ( clk, rst : in std_logic; d : in std_logic_vector(7 downto 0);
           q : out std_logic_vector(7 downto 0) );
end entity;
architecture rtl of nr_e is
    signal cnt : unsigned(7 downto 0);
    signal rx : std_logic_vector(7 downto 0);
begin
    process (clk, rst) is
    begin
        if rst = '1' then
            cnt <= x"00";
        elsif rising_edge(clk) then
            cnt <= cnt + unsigned(d(1 downto 0)) + 1;
            if cnt = 255 and d = x"ff" then
                rx <= d;
            end if;
        end if;
    end process;
    q <= std_logic_vector(cnt);
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity held_tb is end entity;

architecture test of held_tb is
    signal clk, rst, sel, wlo, whi : std_logic := '0';
    signal d : std_logic_vector(7 downto 0) := x"00";
    signal uin : std_logic_vector(3 downto 0);
    signal h_y, h_q, hd_q : std_logic_vector(7 downto 0);
    signal h_n : natural range 0 to 255;
    signal h_c : std_logic;
    signal nr_q : std_logic_vector(7 downto 0);
    signal mon_y, mon_q, mon_d : std_logic_vector(7 downto 0);
begin
    u_h  : entity work.h_e port map (clk, rst, sel, wlo, whi, d, uin,
                                     h_y, h_q, h_n, h_c);
    u_hd : entity work.hd_e port map (clk, h_y, h_q, hd_q);
    u_nr : entity work.nr_e port map (clk, rst, d, nr_q);

    -- Not fused (wait statement): what the VCD records
    mon : process is
    begin
        wait on h_y, h_q, hd_q;
        mon_y <= h_y;
        mon_q <= h_q;
        mon_d <= hd_q;
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
        wait for 3 ns;
        rst <= '0';
        for k in 0 to 299 loop
            d <= std_logic_vector(to_unsigned((k * 37 + 11) mod 256, 8));
            sel <= '1' when (k >= 40 and k < 44) or k = 70 or k = 230
                   else '0';
            wlo <= '1' when k = 50 else '0';
            whi <= '1' when k = 60 else '0';
            case k is
                when 55 => uin <= "0101";
                when 150 => uin <= "01X1";
                when 160 => uin <= "1100";
                when 200 => uin <= "UUUU";
                when 210 => uin <= "0011";
                when others => null;
            end case;
            tick;
            report integer'image(k) & " y=" & to_string(h_y) & " q="
                & to_string(h_q) & " n=" & integer'image(h_n) & " c="
                & std_logic'image(h_c) & " hd=" & to_string(hd_q) & " nr="
                & to_hstring(nr_q);
        end loop;
        wait;
    end process;
end architecture;
VHDL_EOF

unset NVC_ISLAND_SPEC NVC_ISLAND_SPEC_FORCE NVC_ISLAND_SPEC_STATS
unset NVC_ISLAND_SPEC_STRESS NVC_ISLAND_SPEC_RT

nd() { sed '/^\$date/,/\$end/d' $1; }
# Island processes report warnings under the island's name
np() { grep -v '^   Process' $1; }

nvc --std=2008 --work=w_off -a held.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e held_tb \
    -r --ieee-warnings=on --wave=off.vcd --format=vcd 2>&1 \
    | grep -v '^\*\* Debug' > off.log
nvc --std=2008 --work=w_b -a held.vhd
nvc --std=2008 --work=w_b --rtl-islands=hier -e held_tb \
    -r --ieee-warnings=on --wave=b.vcd --format=vcd 2>&1 \
    | grep -v '^\*\* Debug' > b.log
diff -u <(np off.log) <(np b.log)
diff -u <(nd off.vcd) <(nd b.vcd)

export NVC_ISLAND_SPEC=1
# Promote holding values at once, not after the default wait
export NVC_ISLAND_SPEC_HOLD_WAIT=0
nvc --std=2008 --work=w_s -a held.vhd
nvc --std=2008 --work=w_s --rtl-islands=hier -e held_tb
for st in 0 1 2 5; do
    NVC_ISLAND_SPEC_STRESS=$st NVC_ISLAND_ACT_CHECK=1 NVC_ISLAND_SPEC_STATS=2 \
        nvc --std=2008 --work=w_s -r --ieee-warnings=on --wave=s$st.vcd \
        --format=vcd held_tb 2>&1 | grep -v '^\*\* Debug' > s${st}_raw.log
    grep -v '^ISLAND_SPEC' s${st}_raw.log > s$st.log
    diff -u b.log s$st.log
    diff -u <(nd off.vcd) <(nd s$st.vcd)
done
NVC_ISLAND_SPEC_STATS=1 nvc --std=2008 --work=w_s -r --no-island-fast \
    --ieee-warnings=on held_tb 2>&1 | grep -v '^\*\* Debug' > nf_raw.log
diff -u b.log <(grep -v '^ISLAND_SPEC' nf_raw.log)

cat s0_raw.log | grep ISLAND_SPEC
# The 'U' register reaches the output, the next island and TO_INTEGER
grep -q ' 40 y=UUUUUUUU .* n=0 ' b.log
grep -q ' 50 y=00110011 q=UUUU0101 ' b.log
grep -q ' 230 y=UUUUUUUU q=10110101 n=0 ' b.log
grep -q ' 231 y=11101000 .* hd=UUUUUUUU ' b.log
grep -q 'TO_INTEGER: metavalue detected' b.log
# h_e ends in Mode A holding RU; nr_e was promoted once and never deopted
grep -q '^ISLAND_SPEC_RT: island=0 mode=A .* held=1 .* holds=[1-9][0-9]* unholds=[1-9]' s0_raw.log
grep -q '^ISLAND_SPEC_RT:   held sig=[0-9]* RU ' s0_raw.log
grep -q '^ISLAND_SPEC_RT: island=2 mode=A promotions=1 deopts=0 unclean=0 .* held=1 ' s0_raw.log
grep -q '^ISLAND_SPEC_RT: deopts .* unclean=[1-9]' s0_raw.log
