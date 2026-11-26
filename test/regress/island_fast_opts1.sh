set -xe

# Verify --island-fast is ON by default and that all escape hatches
# (--no-island-fast, NVC_ISLAND_FAST=0, --rtl-islands=off, NVC_RTL_ISLANDS=off)
# and CLI-over-env precedence work as documented.

cat >opts_dut.vhd <<'EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity opts_dut is
    port (
        clk         : in  std_logic;
        din         : in  unsigned(7 downto 0);
        obs_out     : out unsigned(7 downto 0);
        pruned_comb : out unsigned(7 downto 0);
        pruned_ff   : out unsigned(7 downto 0)
    );
end entity;

architecture rtl of opts_dut is
    signal q_reg : unsigned(7 downto 0) := x"10";
begin
    p_comb : process (din, q_reg) is
    begin
        pruned_comb <= din xor x"5a";
        obs_out     <= q_reg + din;
    end process;

    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            q_reg     <= din + x"01";
            pruned_ff <= q_reg + x"02";
        end if;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity opts_tb is
end entity;

architecture test of opts_tb is
    signal clk     : std_logic := '0';
    signal din     : unsigned(7 downto 0) := x"04";
    signal obs_out : unsigned(7 downto 0);
begin
    dut : entity work.opts_dut
        port map (
            clk         => clk,
            din         => din,
            obs_out     => obs_out,
            pruned_comb => open,
            pruned_ff   => open
        );

    p_stim : process is
    begin
        wait for 5 ns;
        assert obs_out = x"14" severity failure;
        clk <= '1'; wait for 5 ns; clk <= '0';
        wait for 5 ns;
        assert obs_out = x"09" severity failure;
        wait;
    end process;
end architecture;
EOF

# Elaborate with default hier islands
env -u NVC_ISLAND_FAST -u NVC_RTL_ISLANDS nvc --std=2008 -a opts_dut.vhd
env -u NVC_ISLAND_FAST -u NVC_RTL_ISLANDS nvc --std=2008 -e opts_tb

# 1. Default ON (no env, no CLI flags)
env -u NVC_ISLAND_FAST -u NVC_RTL_ISLANDS NVC_DUMP_ISLANDS=1 \
    nvc --std=2008 -r opts_tb > out_default.log 2>&1
grep -q 'ISLAND_FAST: fast=1 ' out_default.log
grep -q 'pruned=2' out_default.log

# 2. CLI --no-island-fast disables default ON
env -u NVC_ISLAND_FAST -u NVC_RTL_ISLANDS NVC_DUMP_ISLANDS=1 \
    nvc --std=2008 -r --no-island-fast opts_tb > out_cli_off.log 2>&1
grep -q 'ISLAND_HIER:' out_cli_off.log
! grep -q 'ISLAND_FAST:' out_cli_off.log

# 3. CLI --no-island-fast overrides NVC_ISLAND_FAST=1
env -u NVC_RTL_ISLANDS NVC_ISLAND_FAST=1 NVC_DUMP_ISLANDS=1 \
    nvc --std=2008 -r --no-island-fast opts_tb > out_cli_beats_env1.log 2>&1
grep -q 'ISLAND_HIER:' out_cli_beats_env1.log
! grep -q 'ISLAND_FAST:' out_cli_beats_env1.log

# 4. Env NVC_ISLAND_FAST=0 disables default ON
env -u NVC_RTL_ISLANDS NVC_ISLAND_FAST=0 NVC_DUMP_ISLANDS=1 \
    nvc --std=2008 -r opts_tb > out_env_off.log 2>&1
grep -q 'ISLAND_HIER:' out_env_off.log
! grep -q 'ISLAND_FAST:' out_env_off.log

# 5. CLI --island-fast overrides NVC_ISLAND_FAST=0
env -u NVC_RTL_ISLANDS NVC_ISLAND_FAST=0 NVC_DUMP_ISLANDS=1 \
    nvc --std=2008 -r --island-fast opts_tb > out_cli_beats_env0.log 2>&1
grep -q 'ISLAND_FAST: fast=1 ' out_cli_beats_env0.log
grep -q 'pruned=2' out_cli_beats_env0.log

# 6. --rtl-islands=off and NVC_RTL_ISLANDS=off disable islands with default ON
env -u NVC_ISLAND_FAST -u NVC_RTL_ISLANDS NVC_DUMP_ISLANDS=1 \
    nvc --std=2008 --rtl-islands=off -e opts_tb
env -u NVC_ISLAND_FAST -u NVC_RTL_ISLANDS NVC_DUMP_ISLANDS=1 \
    nvc --std=2008 -r opts_tb > out_rtl_off.log 2>&1
! grep -q 'ISLAND_HIER:' out_rtl_off.log
! grep -q 'ISLAND_FAST:' out_rtl_off.log

env -u NVC_ISLAND_FAST NVC_RTL_ISLANDS=off NVC_DUMP_ISLANDS=1 \
    nvc --std=2008 -e opts_tb
env -u NVC_ISLAND_FAST NVC_RTL_ISLANDS=off NVC_DUMP_ISLANDS=1 \
    nvc --std=2008 -r opts_tb > out_rtl_env_off.log 2>&1
! grep -q 'ISLAND_HIER:' out_rtl_env_off.log
! grep -q 'ISLAND_FAST:' out_rtl_env_off.log

# 7. --help documents --no-island-fast
nvc --help 2>&1 | grep -q -- '--no-island-fast'
