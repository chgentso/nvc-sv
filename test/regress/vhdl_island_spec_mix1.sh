set -xe

# Speculative Mode A at run time: direct wires between Mode A and Mode B
# islands.  src_e and sink_e stay in Mode B (they read a signal that is
# never assigned); dst_e is promoted and reads the outputs of src_e (Mode B
# bytes to packed words, some wider than eight bytes) and drives sink_e
# (packed words to bytes).  Most source values repeat from one island call
# to the next, so the copies skip unchanged values; 'X', 'Z' and 'W' values
# make dst_e hold its inputs or go back to Mode B, and the values seen
# before come back afterwards.  The output must be that of
# --rtl-islands=off.

cat >mix1.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- Stays in Mode B: U is never assigned and the comb process reads it
entity src_e is
    port ( clk : in  std_logic;
           k   : in  unsigned(7 downto 0);
           q   : out std_logic_vector(7 downto 0);
           b   : out std_logic;
           w   : out std_logic_vector(15 downto 0);
           d   : out std_logic );
end entity;
architecture rtl of src_e is
    signal u : std_logic;
    signal r : unsigned(7 downto 0) := x"00";
begin
    regs : process (clk) is
    begin
        if rising_edge(clk) then
            r <= k;
        end if;
    end process;
    comb : process (r, u) is
    begin
        d <= u;
        q <= std_logic_vector(r(7 downto 2)) & "00";
        b <= r(1);
        w <= std_logic_vector(r(3 downto 0)) & x"5" & std_logic_vector(r);
        if r(4 downto 0) = "10011" then
            q(3) <= 'X';             -- Holds the Mode A target
            w(9) <= 'Z';
        elsif r(4 downto 0) = "10111" then
            b <= 'W';
        end if;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

-- Mode A: reads the Mode B outputs of SRC_E over direct wires
entity dst_e is
    port ( clk : in  std_logic;
           q   : in  std_logic_vector(7 downto 0);
           b   : in  std_logic;
           w   : in  std_logic_vector(15 downto 0);
           o   : out std_logic_vector(7 downto 0);
           ow  : out std_logic_vector(15 downto 0) );
end entity;
architecture rtl of dst_e is
    signal acc : std_logic_vector(7 downto 0) := x"00";
begin
    comb : process (q, b, w, acc) is
    begin
        -- An 'X', 'Z' or 'W' input gives a clean output
        for i in o'range loop
            if q(i) = '1' then
                o(i) <= not acc(i);
            else
                o(i) <= acc(i);
            end if;
        end loop;
        for i in ow'range loop
            if w(i) = '1' and b = '1' then
                ow(i) <= '1';
            elsif w(i) = '0' and b /= '1' then
                ow(i) <= '1';
            else
                ow(i) <= '0';
            end if;
        end loop;
    end process;
    regs : process (clk) is
    begin
        if rising_edge(clk) then
            acc <= acc(6 downto 0) & acc(7);
            if acc = x"00" then
                acc <= x"01";
            end if;
        end if;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

-- Mode B as well: reads the Mode A outputs of DST_E over direct wires
entity sink_e is
    port ( clk : in  std_logic;
           o   : in  std_logic_vector(7 downto 0);
           ow  : in  std_logic_vector(15 downto 0);
           y   : out std_logic_vector(7 downto 0);
           d   : out std_logic );
end entity;
architecture rtl of sink_e is
    signal u : std_logic;
    signal c : std_logic_vector(7 downto 0);
    signal r : std_logic_vector(7 downto 0) := x"00";
begin
    comb : process (o, ow, u) is
    begin
        d <= u;
        c <= o xor ow(15 downto 8) xor ow(7 downto 0);
    end process;
    regs : process (clk) is
    begin
        if falling_edge(clk) then
            r <= c xor r;
        end if;
    end process;
    y <= r;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mix1_tb is end entity;

architecture test of mix1_tb is
    signal clk : std_logic := '0';
    signal k : unsigned(7 downto 0) := x"00";
    signal q, o : std_logic_vector(7 downto 0);
    signal w, ow : std_logic_vector(15 downto 0);
    signal b, d, d2 : std_logic;
    signal y : std_logic_vector(7 downto 0);
begin
    s : entity work.src_e port map (clk, k, q, b, w, d);
    t : entity work.dst_e port map (clk, q, b, w, o, ow);
    z : entity work.sink_e port map (clk, o, ow, y, d2);

    mon : process (y) is
    begin
        report to_string(y);
    end process;

    stim : process is
    begin
        for i in 0 to 499 loop
            -- Values repeat for several cycles and come back after an
            -- 'X', 'Z' or 'W'
            k <= to_unsigned(i mod 64, 8);
            clk <= '1';
            wait for 5 ns;
            clk <= '0';
            wait for 5 ns;
        end loop;
        wait;
    end process;
end architecture;
VHDL_EOF

unset NVC_ISLAND_SPEC NVC_ISLAND_SPEC_FORCE NVC_ISLAND_SPEC_STATS
unset NVC_ISLAND_SPEC_STRESS NVC_ISLAND_SPEC_RT
export NVC_ISLAND_SPEC_START=0      # Short run: promote from the start
export NVC_ISLAND_SPEC_HOLD_WAIT=0

nvc --std=2008 --work=w_off -a mix1.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e mix1_tb -r 2>&1 \
    | grep -v '^\*\* Debug' > off.log

export NVC_ISLAND_SPEC=1
nvc --std=2008 --work=w_s -a mix1.vhd
nvc --std=2008 --work=w_s --rtl-islands=hier -e mix1_tb
for st in 0 1 3; do
    NVC_ISLAND_SPEC_STRESS=$st NVC_ISLAND_SPEC_STATS=2 \
        nvc --std=2008 --work=w_s -r mix1_tb 2>&1 \
        | grep -v '^\*\* Debug' > s${st}_raw.log
    sed -i '/^\*\* Debug: /d' *.log   # debug builds only: JIT code footprint etc.
    diff -u off.log <(grep -v '^ISLAND_SPEC' s${st}_raw.log)
done
cat s0_raw.log | grep ISLAND_SPEC
grep -q "Note: 1485ns+3: 01011010" off.log
# dst_e runs in Mode A between its Mode B neighbours and holds values
grep -q '^ISLAND_SPEC_RT: island=[0-9]* mode=B .* scope=W_S.MIX1_TB.S$' s0_raw.log
grep -q '^ISLAND_SPEC_RT: island=[0-9]* .* holds=[1-9][0-9]* .* ticks_a=[1-9][0-9][0-9]* scope=W_S.MIX1_TB.T$' s0_raw.log
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_s -r mix1_tb 2>&1 \
    | grep -q 'direct_wires=5'
