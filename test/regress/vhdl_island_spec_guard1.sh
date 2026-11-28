set -xe

# A combinational process that may warn runs in Mode B only when an input
# changed since its last run, which it checks against copies of its inputs.
# Its speculative Mode A variant ran it without updating those copies, so
# after a switch back to Mode B (Y below is 'U' at times) it compared a
# changed input with a stale copy that happened to be equal and did not
# run.  Output must be the same as --rtl-islands=off with
# NVC_ISLAND_SPEC=1, with every eligible island in 2-state from time zero
# (NVC_ISLAND_SPEC_FORCE=A) and with NVC_ISLAND_SPEC_STRESS.

cat >guard1.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- Output Y is '1' or 'U' depending on the value A had at the last edge
entity ch_e is
    port ( clk, en : in std_logic; a : in signed(3 downto 0);
           y : out std_logic );
end entity;
architecture rtl of ch_e is
    signal r : std_logic_vector(22 downto 0);
    signal m : signed(7 downto 0);
begin
    process (clk) is
    begin
        if rising_edge(clk) then
            if en = '1' then
                r <= (others => a(0));
            end if;
        end if;
    end process;
    y <= m(1);
    process (all) is
    begin
        m <= (others => 'U');
        if r(12) = '1' then
            m <= resize(a, 8);
        end if;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity g_e is
    port ( clk, en : in std_logic; k : in unsigned(26 downto 0);
           j : in unsigned(30 downto 0); i2 : in std_logic;
           q : out signed(100 downto 0) );
end entity;
architecture rtl of g_e is
    signal w : std_logic_vector(96 downto 0);
    signal k8, t : unsigned(7 downto 0);
    signal s6 : signed(3 downto 0);
    signal y : std_logic;
begin
    -- Guarded by copies of its inputs in Mode B (it may warn); W is long
    -- enough that its copy is compared only when its change byte is set
    p1: process (all) is
    begin
        q <= resize(s6, 101) + 1;
        if w /= std_logic_vector(resize(j, 13)) & "1101110101010010111001011001000010011011110100"
                & std_logic_vector(resize(k8, 38)) then
            null;
        elsif s6 = "0000" then
            null;
        end if;
    end process;
    -- Reads Y, which is 'U' at times: the island runs in Mode B then
    p2: process (all) is
    begin
        t <= k8;
        if y = '1' then
            null;
        end if;
    end process;
    k8 <= resize(k, 8);
    s6 <= signed'("011") & i2;
    process (clk) is
    begin
        if rising_edge(clk) then
            if en = '0' then
                w <= "0110101000100001011101100100101101110111100011010111011001010000110000011000000011111011111011001";
            end if;
        end if;
    end process;
    u : entity work.ch_e port map (clk, en, s6, y);
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity guard1_tb is end entity;
architecture test of guard1_tb is
    signal clk, en, i2, i2a, i2b, i2c : std_logic := '0';
    signal k : unsigned(26 downto 0) := "110000001000111011001011010";
    signal j : unsigned(30 downto 0);
    signal q : signed(100 downto 0);
begin
    u : entity work.g_e port map (clk, en, k, j, i2, q);
    i2b <= i2a;
    i2c <= i2b;
    i2 <= i2c;
    mon : process (q) is
    begin
        report "q=" & to_hstring(q);
    end process;
    stim : process is
    begin
        for c in 0 to 15 loop
            wait for 10 ns;
            clk <= '1';
            if c = 0 then
                j <= "0110100011101010111100011110111";
            end if;
            if c mod 4 < 2 then
                i2a <= not i2a;
            end if;
            wait for 10 ns;
            clk <= '0';
            if c = 0 then
                en <= '1';
            end if;
        end loop;
        wait;
    end process;
end architecture;
VHDL_EOF

unset NVC_ISLAND_SPEC NVC_ISLAND_SPEC_FORCE NVC_ISLAND_SPEC_STATS
unset NVC_ISLAND_SPEC_STRESS NVC_ISLAND_SPEC_RT

nvc --std=2008 --work=w_off -a guard1.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e guard1_tb -r 2>&1 \
    | grep -v '^\*\* Debug' > off.log
NVC_ISLAND_SPEC=1 nvc --std=2008 --work=w_s -a guard1.vhd
NVC_ISLAND_SPEC=1 nvc --std=2008 --work=w_s -e guard1_tb
nvc --std=2008 --work=w_s -r guard1_tb 2>&1 | grep -v '^\*\* Debug' > s.log
NVC_ISLAND_SPEC_FORCE=A nvc --std=2008 --work=w_s -r guard1_tb 2>&1 \
    | grep -v '^\*\* Debug' > a.log
NVC_ISLAND_SPEC_STRESS=1 nvc --std=2008 --work=w_s -r guard1_tb 2>&1 \
    | grep -v '^\*\* Debug' > st.log
diff -u off.log s.log
diff -u off.log a.log
diff -u off.log st.log
grep -q '90ns+6: q=00000000000000000000000008' off.log
