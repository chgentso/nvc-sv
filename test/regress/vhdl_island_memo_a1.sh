set -xe

# Mode A (--2state=islands) skips the unpack of an island output whose packed
# words did not change since they were last compared with the kernel value.
# The kernel value of an output can nevertheless change behind the island's
# back (force and release from the test bench, a value that flips and returns
# in consecutive deltas): the visible values and the event count must equal
# the Mode B and --rtl-islands=off runs.

cat >memo_a.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;

entity memo_dut is
    port (
        clk : in  std_logic;
        d1  : in  std_logic;
        d70 : in  std_logic_vector(69 downto 0);
        c1  : out std_logic;
        c70 : out std_logic_vector(69 downto 0) );
end entity;

architecture rtl of memo_dut is
    signal r1  : std_logic := '0';
    signal r70 : std_logic_vector(69 downto 0) := (others => '0');
begin
    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            r1  <= d1;
            r70 <= d70;
        end if;
    end process;

    c1  <= not r1;
    c70 <= not r70;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity memo_tb is
end entity;

architecture test of memo_tb is
    signal clk : std_logic := '0';
    signal d1, c1 : std_logic := '0';
    signal d70, c70 : std_logic_vector(69 downto 0) := (others => '0');
    signal n1, n70 : natural := 0;

    procedure tick (signal clk : out std_logic) is
    begin
        wait for 1 ns;
        clk <= '1'; wait for 1 ns; clk <= '0'; wait for 1 ns;
    end procedure;
begin
    dut : entity work.memo_dut port map (clk, d1, d70, c1, c70);

    -- number of value changes seen on the outputs
    p_mon : process (c1, c70) is
    begin
        if c1'event then n1 <= n1 + 1; end if;
        if c70'event then n70 <= n70 + 1; end if;
    end process;

    stim : process is
        variable x : std_logic_vector(69 downto 0) := (others => '0');
    begin
        for k in 0 to 11 loop
            case k is
                when 2 | 3 => null;                     -- unchanged inputs
                when 4 => d1 <= '1'; d70 <= (others => '1');
                when 5 => d1 <= '0'; d70 <= (others => '0');
                when 6 => d1 <= '1'; d70 <= (others => '1');
                when others =>
                    x := std_logic_vector(unsigned'(x"8f3c5a1e2b") + k)
                         & std_logic_vector(to_unsigned(k, 30));
                    d1 <= not d1; d70 <= x;
            end case;
            tick(clk);
            report integer'image(k) & " " & std_logic'image(c1) & " "
                & to_string(c70) & " " & integer'image(n1) & " "
                & integer'image(n70);

            if k = 3 or k = 7 then
                -- force the outputs, then release with unchanged inputs
                c1  <= force '1';
                c70 <= force (69 downto 0 => '1');
                wait for 1 ns;
                report "forced " & std_logic'image(c1) & " " & to_string(c70);
                c1  <= release;
                c70 <= release;
                wait for 1 ns;
                report "released " & std_logic'image(c1) & " " & to_string(c70)
                    & " " & integer'image(n1) & " " & integer'image(n70);
                tick(clk);
                report "after " & std_logic'image(c1) & " " & to_string(c70)
                    & " " & integer'image(n1) & " " & integer'image(n70);
            end if;
        end loop;
        wait;
    end process;
end architecture;
VHDL_EOF

nvc --std=2008 --work=w_off -a memo_a.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e memo_tb -r 2>&1 | grep -v '^\*\* Debug' | sort > off.log

nvc --std=2008 --work=w_b -a memo_a.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_b --rtl-islands=hier -e memo_tb >b_dump.log
nvc --std=2008 --work=w_b -r memo_tb 2>&1 | grep -v '^\*\* Debug' | sort > b.log

nvc --std=2008 --work=w_a -a memo_a.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_a --rtl-islands=hier --2state=islands -e memo_tb >a_dump.log
nvc --std=2008 --work=w_a -r memo_tb 2>&1 | grep -v '^\*\* Debug' | sort > a.log

cat a_dump.log
grep -q released off.log
diff -u off.log b.log
diff -u off.log a.log
grep -q 'MEMO_TB.DUT islands=1 fused_procs=3/3 .* two_state=[1-9]' a_dump.log
