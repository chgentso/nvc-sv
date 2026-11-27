set -xe

# Speculative Mode A deopt stubs: with NVC_ISLAND_SPEC_FORCE=A the Mode A
# variant of an island returns a deopt code instead of computing what
# two-state storage cannot represent, and the island runs in Mode B for that
# call.  Each kind of stub must fire and the output must stay the one of
# Mode B: std_logic literals other than '0'/'1', integer conversions out of
# range (with IEEE warnings), report statements and failing assertions, a
# state that is not all '0'/'1' (no initial values), and a failing range
# check whose message Mode B prints.

cat >deopt.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- '-' in a branch taken from the fifth edge on
entity lit_e is
    port ( clk : in std_logic; din : in unsigned(7 downto 0);
           q : out std_logic_vector(3 downto 0) );
end entity;
architecture rtl of lit_e is
    signal r : std_logic_vector(3 downto 0) := "0000";
    signal n : unsigned(2 downto 0) := "000";
begin
    process (clk) is
    begin
        if rising_edge(clk) then
            n <= n + 1;
            if n = 4 then
                r <= "1--1";
            elsif n(0) = '0' then
                r <= "0101";
            else
                r <= "1010";
            end if;
        end if;
    end process;
    q <= r;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- A comparison with 'U' evaluated on every edge
entity isu_e is
    port ( clk : in std_logic; din : in unsigned(7 downto 0);
           q : out std_logic );
end entity;
architecture rtl of isu_e is
    signal r : std_logic := '0';
    signal d : std_logic := '0';
begin
    process (clk) is
    begin
        if rising_edge(clk) then
            d <= din(0);
            if d = 'U' then
                r <= '1';
            else
                r <= not r;
            end if;
        end if;
    end process;
    q <= r;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- Integer conversions: truncation warning, integer compared with a narrower
-- vector
entity conv_e is
    port ( clk : in std_logic; din : in unsigned(7 downto 0);
           q : out unsigned(3 downto 0); big : out std_logic );
end entity;
architecture rtl of conv_e is
    signal r : unsigned(3 downto 0) := "0000";
    signal b : std_logic := '0';
begin
    process (clk) is
    begin
        if rising_edge(clk) then
            r <= to_unsigned(to_integer(din) mod 24, 4);
            if din < 300 and din > 200 then
                b <= '1';
            else
                b <= '0';
            end if;
        end if;
    end process;
    q <= r;
    big <= b;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- Report statement and assertion in a clocked process
entity rep_e is
    port ( clk : in std_logic; din : in unsigned(7 downto 0);
           q : out unsigned(7 downto 0) );
end entity;
architecture rtl of rep_e is
    signal r : unsigned(7 downto 0) := x"00";
begin
    process (clk) is
    begin
        if rising_edge(clk) then
            r <= r + 1;
            if r(2 downto 0) = "101" then
                report "count " & to_hstring(r);
            end if;
            assert r /= x"09" report "count is nine" severity note;
        end if;
    end process;
    q <= r;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- No initial values: the state holds 'U' until the first edge
entity nz_e is
    port ( clk : in std_logic; din : in unsigned(7 downto 0);
           q : out std_logic_vector(7 downto 0) );
end entity;
architecture rtl of nz_e is
    signal r   : std_logic_vector(7 downto 0);
    signal tmp : unsigned(7 downto 0);
begin
    tmp <= din + 1;
    process (clk) is
    begin
        if rising_edge(clk) then
            r <= std_logic_vector(tmp);
        end if;
    end process;
    q <= r;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity deopt_tb is end entity;

architecture test of deopt_tb is
    signal clk  : std_logic := '0';
    signal din  : unsigned(7 downto 0) := x"00";
    signal lit  : std_logic_vector(3 downto 0);
    signal isu  : std_logic;
    signal conv : unsigned(3 downto 0);
    signal big  : std_logic;
    signal cnt  : unsigned(7 downto 0);
    signal nz   : std_logic_vector(7 downto 0);
begin
    u_lit  : entity work.lit_e port map (clk, din, lit);
    u_isu  : entity work.isu_e port map (clk, din, isu);
    u_conv : entity work.conv_e port map (clk, din, conv, big);
    u_rep  : entity work.rep_e port map (clk, din, cnt);
    u_nz   : entity work.nz_e port map (clk, din, nz);

    stim : process is
    begin
        for k in 0 to 15 loop
            din <= to_unsigned((k * 61 + 7) mod 256, 8);
            wait for 5 ns;
            clk <= '1';
            wait for 5 ns;
            clk <= '0';
            report integer'image(k) & " " & to_string(lit) & " "
                & std_logic'image(isu) & " " & to_string(conv) & " "
                & std_logic'image(big) & " " & to_hstring(cnt) & " "
                & to_string(nz);
        end loop;
        wait;
    end process;
end architecture;
VHDL_EOF

# A failing range check inside an island: Mode B prints the message
cat >range.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity range_tb is end entity;

architecture test of range_tb is
    function half (n : natural) return natural is
    begin
        return n / 2;
    end function;
    signal clk : std_logic := '0';
    signal a   : unsigned(3 downto 0) := x"0";
    signal q   : unsigned(3 downto 0) := x"0";
    signal r   : unsigned(3 downto 0) := x"0";
begin
    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            q <= to_unsigned(half(to_integer(a) - 5), 4);
        end if;
    end process;
    p_ff2 : process (clk) is
    begin
        if rising_edge(clk) then
            r <= q + 1;
        end if;
    end process;

    stim : process is
    begin
        for k in 15 downto 0 loop
            a <= to_unsigned(k, 4);
            wait for 5 ns;
            clk <= '1';
            wait for 5 ns;
            clk <= '0';
            report integer'image(k) & " " & to_string(q) & " " & to_string(r);
        end loop;
        wait;
    end process;
end architecture;
VHDL_EOF

unset NVC_ISLAND_SPEC NVC_ISLAND_SPEC_FORCE NVC_ISLAND_SPEC_STATS

run() {
    local mode=$1 top=$2; shift 2
    nvc --std=2008 --work=w_$mode -a deopt.vhd range.vhd
    nvc --std=2008 --work=w_$mode "$@" -e $top
    nvc --std=2008 --work=w_$mode -r --ieee-warnings=on $top 2>&1 \
        | grep -v '^\*\* Debug' || true
}

run b deopt_tb --rtl-islands=hier > b.log
NVC_ISLAND_SPEC=1 NVC_ISLAND_SPEC_FORCE=A NVC_ISLAND_SPEC_STATS=1 \
   run s deopt_tb --rtl-islands=hier > s_raw.log
grep -v '^ISLAND_SPEC:' s_raw.log > s.log
cat s_raw.log
grep -q 'vector truncated' b.log
grep -q 'count is nine' b.log
grep -q 'count 0D' b.log
diff -u b.log s.log

# Sum each counter over the islands: every kind of stub fired, and the
# islands still made calls in Mode A
total() {
    grep '^ISLAND_SPEC: island=' s_raw.log \
        | sed -n "s/.* $1=\([0-9]*\).*/\1/p" | awk '{ s += $1 } END { print s + 0 }'
}
[ "$(total calls_a)" -gt 0 ]
[ "$(total literal)" -gt 0 ]
[ "$(total conv)" -gt 0 ]
[ "$(total report)" -gt 0 ]
[ "$(total state)" -gt 0 ]

run rb range_tb --rtl-islands=hier > rb.log
NVC_ISLAND_SPEC=1 NVC_ISLAND_SPEC_FORCE=A run rs range_tb --rtl-islands=hier \
   > rs.log
cat rs.log
grep -q 'value -1 outside of NATURAL range' rb.log
diff -u rb.log rs.log
