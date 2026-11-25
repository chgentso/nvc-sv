#!/bin/bash -ex
set -ex
#
# Test VHDL Compiled RTL Islands (--rtl-islands=module|hier) and
# VHDL Island-Only 2-State (--2state=off|islands|all)
#

if [ -n "${ASAN_OPTIONS:-}" ]; then
  export ASAN_OPTIONS="$ASAN_OPTIONS:detect_leaks=0"
fi

WORKDIR=$(mktemp -d)
trap 'rm -rf "$WORKDIR"' EXIT
export XDG_CACHE_HOME="$WORKDIR/cache"
mkdir -p "$XDG_CACHE_HOME"
cd "$WORKDIR"

# --- Subtest 1 & 2: Pure VHDL RTL (records, to/downto, numeric_std, async reset, RAM, 9-state boundary) ---
cat > rtl_pure.vhd <<'EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package rtl_pkg is
  type ctrl_rec_t is record
    en  : std_logic;
    op  : unsigned(1 downto 0);
    imm : std_logic_vector(0 to 7);
  end record;
  type ram_t is array (0 to 7) of std_logic_vector(7 downto 0);
end package;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.rtl_pkg.all;

entity dut is
  port (
    clk      : in  std_logic;
    rst      : in  std_logic;
    ctrl     : in  ctrl_rec_t;
    din      : in  std_logic_vector(7 downto 0);
    addr     : in  std_logic_vector(2 downto 0);
    dout     : out std_logic_vector(7 downto 0);
    ram_out  : out std_logic_vector(7 downto 0);
    tristate : out std_logic_vector(3 downto 0);
    match_ok : out std_logic
  );
end entity;

architecture rtl of dut is
  signal acc_reg  : unsigned(7 downto 0) := (others => '0');
  signal next_acc : unsigned(7 downto 0);
  signal rev_vec  : std_logic_vector(0 to 7);
  signal ram      : ram_t := (others => (others => '0'));
  signal z_sig    : std_logic_vector(3 downto 0);
begin
  -- Combinational datapath using numeric_std, records, and to/downto slices
  process(ctrl, din, acc_reg)
    variable tmp : unsigned(7 downto 0);
  begin
    tmp := unsigned(din);
    rev_vec(0 to 3) <= ctrl.imm(4 to 7);
    rev_vec(4 to 7) <= ctrl.imm(0 to 3);
    case ctrl.op is
      when "00" =>
        next_acc <= acc_reg + tmp + resize(unsigned(ctrl.imm), 8);
      when "01" =>
        next_acc <= acc_reg - tmp;
      when "10" =>
        next_acc <= acc_reg xor unsigned(din);
      when others =>
        next_acc <= shift_left(acc_reg, 1) or to_unsigned(1, 8);
    end case;
  end process;

  -- Clocked FF with async reset + RAM array
  process(clk, rst)
  begin
    if rst = '1' then
      acc_reg <= (others => '0');
    elsif rising_edge(clk) then
      if ctrl.en = '1' then
        acc_reg <= next_acc;
        ram(to_integer(unsigned(addr))) <= std_logic_vector(next_acc);
      end if;
    end if;
  end process;

  dout <= std_logic_vector(acc_reg) xor rev_vec;
  ram_out <= ram(to_integer(unsigned(addr)));

  -- 9-state specific process (must remain 9-state unfused in --2state>=islands)
  process(ctrl, din)
  begin
    if ctrl.en = '0' then
      z_sig <= (others => 'Z');
    else
      z_sig <= din(3 downto 0);
    end if;
    if std_match(din, "1010----") and not is_x(din) then
      match_ok <= '1';
    else
      match_ok <= '0';
    end if;
  end process;
  tristate <= z_sig;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.rtl_pkg.all;

entity tb_pure is
end entity;

architecture sim of tb_pure is
  signal clk      : std_logic := '0';
  signal rst      : std_logic := '1';
  signal ctrl     : ctrl_rec_t := (en => '0', op => "00", imm => x"00");
  signal din      : std_logic_vector(7 downto 0) := x"00";
  signal addr     : std_logic_vector(2 downto 0) := "000";
  signal dout     : std_logic_vector(7 downto 0);
  signal ram_out  : std_logic_vector(7 downto 0);
  signal tristate : std_logic_vector(3 downto 0);
  signal match_ok : std_logic;
begin
  u_dut: entity work.dut
    port map (
      clk => clk, rst => rst, ctrl => ctrl, din => din, addr => addr,
      dout => dout, ram_out => ram_out, tristate => tristate, match_ok => match_ok
    );

  process
    variable u_test : std_logic := 'U';
    variable x_test : std_logic_vector(3 downto 0) := "ZXHL";
  begin
    -- Check external VHDL 9-state stays 100% intact in all modes
    assert u_test = 'U' severity failure;
    assert is_x(x_test) severity failure;

    wait for 5 ns;
    clk <= '1'; wait for 5 ns; clk <= '0';
    report "T0 rst dout=" & to_hstring(dout) & " z=" & to_string(tristate);

    rst <= '0';
    ctrl <= (en => '1', op => "00", imm => x"12");
    din <= x"A5";
    addr <= "001";
    wait for 5 ns; clk <= '1'; wait for 5 ns; clk <= '0';
    report "T1 dout=" & to_hstring(dout) & " ram=" & to_hstring(ram_out)
           & " m=" & to_string(match_ok) & " z=" & to_hstring(tristate);

    ctrl.op <= "01";
    din <= x"0F";
    addr <= "010";
    wait for 5 ns; clk <= '1'; wait for 5 ns; clk <= '0';
    report "T2 dout=" & to_hstring(dout) & " ram=" & to_hstring(ram_out);

    ctrl.op <= "10";
    din <= x"3C";
    addr <= "001";
    wait for 5 ns; clk <= '1'; wait for 5 ns; clk <= '0';
    report "T3 dout=" & to_hstring(dout) & " ram=" & to_hstring(ram_out);

    ctrl.en <= '0';
    ctrl.op <= "11";
    din <= "UUUU0000";
    wait for 5 ns;
    report "T4 9s m=" & to_string(match_ok) & " z=" & to_string(tristate);
    wait;
  end process;
end architecture;
EOF

nvc -a rtl_pure.vhd

# Compare full report stdout + FST equivalence across off / module / hier
nvc --rtl-islands=off --2state=off -e tb_pure && nvc --rtl-islands=off --2state=off -r tb_pure --wave=w_off.fst > raw_off.txt 2>&1
nvc --rtl-islands=module --2state=off --dump-islands -e tb_pure && nvc --rtl-islands=module --2state=off -r tb_pure --wave=w_mod.fst > raw_mod.txt 2>&1
nvc --rtl-islands=hier --2state=off --dump-islands -e tb_pure && nvc --rtl-islands=hier --2state=off -r tb_pure --wave=w_hier.fst > raw_hier.txt 2>&1

grep -E '^\*\* Note:.*T[0-9]' raw_off.txt > rep_off.txt
test -s rep_off.txt
grep -E '^\*\* Note:.*T[0-9]' raw_mod.txt > rep_mod.txt
grep -E '^\*\* Note:.*T[0-9]' raw_hier.txt > rep_hier.txt
diff -u rep_off.txt rep_mod.txt
diff -u rep_off.txt rep_hier.txt

if command -v fst2vcd >/dev/null 2>&1; then
  fst2vcd -f w_off.fst -o w_off.vcd
  fst2vcd -f w_mod.fst -o w_mod.vcd
  fst2vcd -f w_hier.fst -o w_hier.vcd
  sed '/^\$date/,/^\$end/d' w_off.vcd > w_off_nd.vcd
  sed '/^\$date/,/^\$end/d' w_mod.vcd > w_mod_nd.vcd
  sed '/^\$date/,/^\$end/d' w_hier.vcd > w_hier_nd.vcd
  diff -u w_off_nd.vcd w_mod_nd.vcd
  diff -u w_off_nd.vcd w_hier_nd.vcd
fi

# Check --2state=islands and --2state=all both enable 2-state VHDL islands
nvc --rtl-islands=hier --2state=islands --dump-islands -e tb_pure > elab_2s_isl.txt 2>&1
grep -q "VHDL_ISLAND_ELAB:.*two_state=[1-9]" elab_2s_isl.txt
nvc --rtl-islands=hier --2state=islands -r tb_pure > raw_2s_isl.txt 2>&1
grep -E '^\*\* Note:.*T[0-9]' raw_2s_isl.txt > rep_2s_isl.txt
diff -u rep_off.txt rep_2s_isl.txt

nvc --rtl-islands=hier --2state=all --dump-islands -e tb_pure > elab_2s_all.txt 2>&1
grep -q "VHDL_ISLAND_ELAB:.*two_state=[1-9]" elab_2s_all.txt
nvc --rtl-islands=hier --2state=all -r tb_pure > raw_2s_all.txt 2>&1
grep -E '^\*\* Note:.*T[0-9]' raw_2s_all.txt > rep_2s_all.txt
diff -u rep_off.txt rep_2s_all.txt

# --- Subtest 3: Mixed VHDL + SV islands under hier --2state=islands ---
cat > sv_add.sv <<'EOF'
module sv_add (
  input  logic       clk,
  input  logic [7:0] a,
  input  logic [7:0] b,
  output logic [7:0] y
);
  logic [7:0] sum_w;
  assign sum_w = a + b + 8'h01;
  always_ff @(posedge clk)
    y <= sum_w;
endmodule
EOF

cat > mixed_top.vhd <<'EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_stage is
  port (
    clk : in  std_logic;
    din : in  std_logic_vector(7 downto 0);
    q   : out std_logic_vector(7 downto 0)
  );
end entity;

architecture rtl of vhdl_stage is
  signal r : unsigned(7 downto 0) := (others => '0');
begin
  process(clk)
  begin
    if rising_edge(clk) then
      r <= unsigned(din) xor x"55";
    end if;
  end process;
  q <= std_logic_vector(r);
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tb_mixed is
end entity;

architecture sim of tb_mixed is
  component sv_add is
    port (
      clk : in  std_logic;
      a   : in  std_logic_vector(7 downto 0);
      b   : in  std_logic_vector(7 downto 0);
      y   : out std_logic_vector(7 downto 0)
    );
  end component;
  signal clk   : std_logic := '0';
  signal din   : std_logic_vector(7 downto 0) := x"10";
  signal v_out : std_logic_vector(7 downto 0);
  signal s_out : std_logic_vector(7 downto 0);
begin
  u_v: entity work.vhdl_stage port map (clk => clk, din => din, q => v_out);
  u_s: sv_add port map (clk => clk, a => v_out, b => x"03", y => s_out);

  process
  begin
    for i in 1 to 4 loop
      din <= std_logic_vector(to_unsigned(i * 17, 8));
      wait for 5 ns; clk <= '1'; wait for 5 ns; clk <= '0';
      report "M" & integer'image(i) & " v=" & to_hstring(v_out) & " s=" & to_hstring(s_out);
    end loop;
    wait;
  end process;
end architecture;
EOF

nvc -a sv_add.sv mixed_top.vhd
nvc --rtl-islands=off --2state=off -e tb_mixed && nvc --rtl-islands=off --2state=off -r tb_mixed > mix_off.txt 2>&1
nvc --rtl-islands=hier --2state=islands --dump-islands -e tb_mixed > mix_elab.txt 2>&1
grep -q "VHDL_ISLAND_ELAB:" mix_elab.txt
grep -q "ISLAND_ELAB:" mix_elab.txt
nvc --rtl-islands=hier --2state=islands -r tb_mixed > mix_hier.txt 2>&1
grep -E '^\*\* Note:.*M[0-9]' mix_off.txt > mrep_off.txt
test -s mrep_off.txt
grep -E '^\*\* Note:.*M[0-9]' mix_hier.txt > mrep_hier.txt
diff -u mrep_off.txt mrep_hier.txt

# --- Subtest 4 & 5: Unfused VHDL process/assert (SIG_F_EXT_READ) + AOT cache isolation ---
cat > unfused_obs.vhd <<'EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tb_obs is
end entity;

architecture sim of tb_obs is
  signal clk : std_logic := '0';
  signal d   : unsigned(7 downto 0) := x"00";
  signal q   : unsigned(7 downto 0) := x"00";
  signal c   : unsigned(7 downto 0);
begin
  -- Comb inside island
  c <= d + 1;

  -- FF inside island
  process(clk)
  begin
    if rising_edge(clk) then
      q <= c;
    end if;
  end process;

  -- Unfused process (has report/assert) reading island outputs
  process(q, c)
  begin
    report "OBS c=" & to_hstring(c) & " q=" & to_hstring(q);
    assert to_integer(c) >= 0 severity failure;
  end process;

  process
  begin
    wait for 5 ns; d <= x"0A"; wait for 5 ns; clk <= '1'; wait for 5 ns; clk <= '0';
    wait for 5 ns; d <= x"20"; wait for 5 ns; clk <= '1'; wait for 5 ns; clk <= '0';
    wait;
  end process;
end architecture;
EOF

nvc -a unfused_obs.vhd
nvc --rtl-islands=off -e tb_obs && nvc --rtl-islands=off -r tb_obs > obs_off.txt 2>&1
# Test repeated elab+run across off / islands to verify JIT cache isolation
for ts in off islands; do
  nvc --rtl-islands=hier --2state=$ts --dump-islands -e tb_obs > obs_elab_$ts.txt 2>&1
  if [ "$ts" = "off" ]; then
    grep -q "VHDL_ISLAND_ELAB:.*two_state=0" obs_elab_$ts.txt
  else
    grep -q "VHDL_ISLAND_ELAB:.*two_state=[1-9]" obs_elab_$ts.txt
  fi
  nvc --rtl-islands=hier --2state=$ts -r tb_obs > obs_run_$ts.txt 2>&1
  grep -E '^\*\* Note:.*OBS' obs_off.txt > oref.txt
  test -s oref.txt
  grep -E '^\*\* Note:.*OBS' obs_run_$ts.txt > orun.txt
  diff -u oref.txt orun.txt
done

# Test 8: VHDL persistent process variable (initializer runs only at t=0) must not be reset each clock
cat > persist_var.vhd <<'EOF'
library ieee;
use ieee.std_logic_1164.all;
entity tb_persist_var is end entity;
architecture rtl of tb_persist_var is
  type st_t is (idle, run, done);
  signal clk : std_logic := '0';
  signal st  : st_t := idle;
begin
  p_st: process (clk) is
    variable ticks : integer := 0;
  begin
    if rising_edge(clk) then
      ticks := ticks + 1;
      if ticks = 3 then
        st <= run;
      elsif ticks = 5 then
        st <= done;
      end if;
    end if;
  end process;
  process begin
    for i in 1 to 6 loop
      wait for 5 ns; clk <= '1'; wait for 5 ns; clk <= '0';
      report "PVAR i=" & integer'image(i) & " st=" & st_t'image(st);
    end loop;
    wait;
  end process;
end architecture;
EOF
nvc -a persist_var.vhd
nvc --rtl-islands=off -e tb_persist_var && nvc --rtl-islands=off -r tb_persist_var > pv_off.txt 2>&1
nvc --rtl-islands=hier -e tb_persist_var && nvc --rtl-islands=hier -r tb_persist_var > pv_hier.txt 2>&1
grep -E '^\*\* Note:.*PVAR' pv_off.txt > pv_ref.txt
grep -E '^\*\* Note:.*PVAR' pv_hier.txt > pv_run.txt
grep -qi "PVAR i=5 st=done" pv_ref.txt
diff -u pv_ref.txt pv_run.txt

echo "PASS vhdl_island1"
