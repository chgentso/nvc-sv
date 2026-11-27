set -xe

# NVC_DUMP_ISLANDS reports why a process is not fused and which fused
# processes keep an island from having a Mode A (--2state=islands) variant.

cat >reasons.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;

entity reasons_dut is
    port (
        clk   : in  std_logic;
        a     : in  std_logic_vector(7 downto 0);
        flag  : out std_logic;
        mtch  : out std_logic;
        dc    : out std_logic;
        q_out : out std_logic_vector(7 downto 0);
        tick  : out std_logic );
end entity;

architecture rtl of reasons_dut is
    signal q, d : std_logic_vector(7 downto 0) := (others => '0');
begin
    p_c : process (a, q) is
    begin
        d <= a xor q;
    end process;

    p_reg : process (clk) is
    begin
        if rising_edge(clk) then
            q <= d;
        end if;
    end process;

    p_x : process (q) is
    begin
        if is_x(q) then flag <= '1'; else flag <= '0'; end if;
    end process;

    p_m : process (q) is
    begin
        if q(0) ?= '1' then mtch <= '1'; else mtch <= '0'; end if;
    end process;


    p_dc : process (q) is
    begin
        if q(1) = '1' then dc <= '1'; else dc <= '-'; end if;
    end process;

    p_out : process (q) is
    begin
        q_out <= q;
    end process;

    p_wait : process is
    begin
        tick <= '0';
        wait for 1 ns;
        tick <= '1';
        wait;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity reasons_tb is
end entity;

architecture test of reasons_tb is
    signal clk : std_logic := '0';
    signal a, q_out : std_logic_vector(7 downto 0) := (others => '0');
    signal flag, mtch, dc, tick : std_logic;
begin
    dut : entity work.reasons_dut
        port map (clk, a, flag, mtch, dc, q_out, tick);

    stim : process is
    begin
        a <= x"5a";
        clk <= '1'; wait for 5 ns; clk <= '0'; wait for 5 ns;
        clk <= '1'; wait for 5 ns; clk <= '0'; wait for 5 ns;
        assert q_out = x"5a";
        wait;
    end process;
end architecture;
VHDL_EOF

export NVC_RTL_ISLANDS=hier NVC_DUMP_ISLANDS=1

# Mode B: the processes using IS_X and a '-' literal are fused but have no
# Mode A variant; the wait-for process is not fused at all (and ?= is not
# lowered in Mode B)

nvc --std=2008 -a reasons.vhd -e reasons_tb >mode_b.log
cat mode_b.log
grep -q 'VHDL_ISLAND_ELAB: .*REASONS_TB.DUT islands=1 fused_procs=5/7' mode_b.log
grep -q 'VHDL_ISLAND_REJECT: .*P_WAIT reason=shape' mode_b.log
grep -q 'VHDL_ISLAND_ABLOCK: .*P_X block=A5 IS_X/STD_MATCH' mode_b.log
grep -q 'VHDL_ISLAND_ABLOCK: .*P_DC block=A3 non-01 literal' mode_b.log
! grep -q 'VHDL_ISLAND_ABLOCK: .*P_REG' mode_b.log
grep -q 'VHDL_ISLAND_INFO: .*REASONS_TB.DUT island=0 procs=5 mode=B a_variant=no reason=A5 IS_X/STD_MATCH; A3 non-01 literal' mode_b.log

# Mode A: the same processes are rejected with their reason
NVC_2STATE=islands nvc --std=2008 -a reasons.vhd -e reasons_tb >mode_a.log
cat mode_a.log
grep -q 'VHDL_ISLAND_REJECT: .*P_X reason=A5 IS_X/STD_MATCH' mode_a.log
grep -q 'VHDL_ISLAND_REJECT: .*P_M reason=A6 matching operator' mode_a.log
grep -q 'VHDL_ISLAND_REJECT: .*P_DC reason=A3 non-01 literal' mode_a.log
grep -q 'VHDL_ISLAND_REJECT: .*P_WAIT reason=shape' mode_a.log
grep -q 'VHDL_ISLAND_INFO: .*mode=A a_variant=yes' mode_a.log

# The dump does not change the simulation
unset NVC_DUMP_ISLANDS
nvc --std=2008 -a reasons.vhd -e reasons_tb -r
