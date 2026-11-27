set -xe

# The ABLOCK lines come from the Mode A trial of the default build
unset NVC_ISLAND_SPEC NVC_ISLAND_SPEC_FORCE

# NVC_DUMP_ISLANDS reports why a process is not fused and which fused
# processes keep an island from having a Mode A (--2state=islands) variant.

cat >reasons.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity reasons_dut is
    port (
        clk   : in  std_logic;
        a     : in  std_logic_vector(7 downto 0);
        dco   : out std_logic;
        rdat  : out std_logic_vector(31 downto 0);
        waddr : in  std_logic_vector(5 downto 0);
        raddr : in  std_logic_vector(5 downto 0);
        q_out : out std_logic_vector(7 downto 0);
        tick  : out std_logic );
end entity;

architecture rtl of reasons_dut is
    signal q, d : std_logic_vector(7 downto 0) := (others => '0');
    signal dc : std_logic;
    type ram_t is array (0 to 63) of std_logic_vector(31 downto 0);
    signal mem : ram_t := (others => (others => '0'));
begin
    p_ram : process (clk) is
    begin
        if rising_edge(clk) then
            mem(to_integer(unsigned(waddr))) <= d & d & d & d;
        end if;
    end process;

    p_rd : process (mem, raddr) is
    begin
        rdat <= mem(to_integer(unsigned(raddr)));
    end process;

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


    p_dc : process (q) is
    begin
        if q(1) = '1' then dc <= '1'; else dc <= '-'; end if;
    end process;

    p_dco : process (dc) is
    begin
        dco <= dc;
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
    signal dco, tick : std_logic;
    signal rdat : std_logic_vector(31 downto 0);
    signal waddr, raddr : std_logic_vector(5 downto 0) := (others => '0');
begin
    dut : entity work.reasons_dut
        port map (clk, a, dco, rdat, waddr, raddr, q_out, tick);

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

# Mode B: the sparse RAM, the '-' literal and its reader are fused but keep
# the island from having a Mode A variant; the wait-for process is not fused
nvc --std=2008 -a reasons.vhd -e reasons_tb >mode_b.log
cat mode_b.log
grep -q 'VHDL_ISLAND_ELAB: .*REASONS_TB.DUT islands=1 fused_procs=7/8' mode_b.log
grep -q 'VHDL_ISLAND_REJECT: .*P_WAIT reason=shape' mode_b.log
grep -q 'VHDL_ISLAND_ABLOCK: .*P_RAM block=A1 ' mode_b.log
grep -q 'VHDL_ISLAND_ABLOCK: .*P_DC block=A3 non-01 literal' mode_b.log
grep -q 'VHDL_ISLAND_ABLOCK: .*P_DCO block=A4 ' mode_b.log
! grep -q 'VHDL_ISLAND_ABLOCK: .*P_REG' mode_b.log
grep -q 'VHDL_ISLAND_INFO: .*REASONS_TB.DUT island=0 procs=7 mode=B a_variant=no reason=A1 sparse RAM; .*A3 non-01 literal' mode_b.log

# Mode A: the same processes are rejected with their reason
NVC_2STATE=islands nvc --std=2008 -a reasons.vhd -e reasons_tb >mode_a.log
cat mode_a.log
grep -q 'VHDL_ISLAND_REJECT: .*P_RAM reason=A1 ' mode_a.log
grep -q 'VHDL_ISLAND_REJECT: .*P_DC reason=A3 non-01 literal' mode_a.log
grep -q 'VHDL_ISLAND_REJECT: .*P_DCO reason=A4 ' mode_a.log
grep -q 'VHDL_ISLAND_REJECT: .*P_WAIT reason=shape' mode_a.log
grep -q 'VHDL_ISLAND_INFO: .*mode=A a_variant=yes' mode_a.log

# The dump does not change the simulation
unset NVC_DUMP_ISLANDS
nvc --std=2008 -a reasons.vhd -e reasons_tb -r
