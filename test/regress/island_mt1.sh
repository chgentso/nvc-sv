#!/bin/bash -ex
set -xe

if [ -n "${ASAN_OPTIONS:-}" ]; then
  export ASAN_OPTIONS="$ASAN_OPTIONS:detect_leaks=0"
fi

# --- Subtest 1: CLI & env option validation ---
cat > small_mt.sv <<'EOF'
module small_unit (
  input  logic       clk,
  input  logic       rst_n,
  input  logic [7:0] d,
  output logic [7:0] q
);
  logic [7:0] w;
  always_comb w = d ^ 8'h5a;
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) q <= 8'h00;
    else        q <= w + 8'h01;
  end
endmodule

module tb_small_mt;
  logic       clk = 1'b0, rst_n = 1'b0;
  logic [7:0] d = 8'h11, q;
  small_unit dut (.clk(clk), .rst_n(rst_n), .d(d), .q(q));
  always #5 clk = ~clk;
  initial begin
    #12 rst_n = 1'b1;
    for (int i = 0; i < 8; i++) begin
      @(negedge clk);
      d = (i * 19 + 3) & 8'hff;
      @(posedge clk);
      #1;
      $display("SMALL %0d q=%02h", i, q);
    end
    $finish;
  end
endmodule
EOF

nvc --work=work_small -a small_mt.sv
nvc --work=work_small --rtl-islands=hier --2state=islands -e tb_small_mt
nvc --work=work_small --threads=1 -r tb_small_mt > small_t1.out
nvc --work=work_small --threads=4 -r tb_small_mt > small_t4.out
nvc --work=work_small --threads=auto -r tb_small_mt > small_ta.out
nvc --work=work_small -r --threads=4 tb_small_mt > small_r4.out
NVC_THREADS=2 nvc --work=work_small -r tb_small_mt > small_e2.out
NVC_THREADS=auto nvc --work=work_small -r tb_small_mt > small_ea.out
diff -u small_t1.out small_t4.out
diff -u small_t1.out small_ta.out
diff -u small_t1.out small_r4.out
diff -u small_t1.out small_e2.out
diff -u small_t1.out small_ea.out

! nvc --work=work_small --threads=0 -r tb_small_mt 2>/dev/null
! nvc --work=work_small --threads=999 -r tb_small_mt 2>/dev/null
! NVC_THREADS=bad nvc --work=work_small -r tb_small_mt 2>/dev/null

# --- Subtest 2: Multi-cluster SV RTL (36 hierarchical 2-stage clusters, >100 cycles) ---
cat > sv_cluster_mt.sv <<'EOF'
module sv_stage_a #(parameter int SEED = 1) (
  input  logic        clk,
  input  logic        rst_n,
  input  logic [15:0] din,
  output logic [15:0] dout,
  output logic [15:0] comb_o
);
  logic [15:0] c0, c1, q;
  always_comb begin
    c0 = (din ^ SEED[15:0]) + {din[7:0], din[15:8]};
    c1 = c0 ^ (q << 1);
    comb_o = c1;
  end
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) q <= SEED[15:0];
    else        q <= c1;
  end
  assign dout = q;
endmodule

module sv_stage_b #(parameter int TAG = 3) (
  input  logic        clk,
  input  logic        rst_n,
  input  logic [15:0] reg_in,
  input  logic [15:0] comb_in,
  output logic [15:0] y
);
  logic [15:0] mix, acc;
  always_comb mix = (reg_in + comb_in) ^ TAG[15:0];
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) acc <= 16'h0000;
    else        acc <= acc + mix;
  end
  assign y = acc ^ mix;
endmodule

module sv_cluster #(parameter int ID = 0) (
  input  logic        clk,
  input  logic        rst_n,
  input  logic [15:0] din,
  output logic [15:0] y
);
  logic [15:0] a_q, a_c;
  sv_stage_a #(.SEED(ID * 17 + 5)) u_a (
    .clk(clk), .rst_n(rst_n), .din(din), .dout(a_q), .comb_o(a_c)
  );
  sv_stage_b #(.TAG(ID * 31 + 9)) u_b (
    .clk(clk), .rst_n(rst_n), .reg_in(a_q), .comb_in(a_c), .y(y)
  );
endmodule

module tb_sv_cluster_mt;
  localparam int NC = 36;
  logic        clk = 1'b0, rst_n = 1'b0;
  logic [15:0] din [0:NC-1];
  logic [15:0] y   [0:NC-1];

  for (genvar i = 0; i < NC; i++) begin : g_cl
    sv_cluster #(.ID(i)) u_cl (
      .clk(clk), .rst_n(rst_n), .din(din[i]), .y(y[i])
    );
  end

  always #5 clk = ~clk;

  initial begin
    for (int i = 0; i < NC; i++)
      din[i] = 16'(i * 13 + 7);
    #12 rst_n = 1'b1;
    for (int cyc = 0; cyc < 130; cyc++) begin
      @(negedge clk);
      for (int i = 0; i < NC; i++)
        din[i] = 16'((cyc * 37 + i * 19 + 1) & 16'hffff);
      @(posedge clk);
      #1;
      if (cyc < 4 || cyc == 64 || cyc == 129) begin
        logic [15:0] xsum;
        xsum = 16'h0000;
        for (int i = 0; i < NC; i++)
          xsum ^= y[i];
        $display("SV cyc=%0d y0=%04h ylast=%04h xsum=%04h",
                 cyc, y[0], y[NC-1], xsum);
      end
    end
    $finish;
  end
endmodule
EOF

nvc --work=work_sv -a sv_cluster_mt.sv

check_sv_mode() {
  local isl_mode=$1
  local two_mode=$2
  local full=$3
  local tag="sv_${isl_mode}_${two_mode}"
  nvc --work=work_sv --rtl-islands="$isl_mode" --2state="$two_mode" -e tb_sv_cluster_mt
  if [ "$full" = "1" ]; then
    nvc --work=work_sv -r --threads=1 --wave="${tag}_t1.fst" tb_sv_cluster_mt > "${tag}_t1.out"
    nvc --work=work_sv -r --threads=2 --wave="${tag}_t2.fst" tb_sv_cluster_mt > "${tag}_t2.out"
    nvc --work=work_sv -r --threads=4 --wave="${tag}_t4.fst" tb_sv_cluster_mt > "${tag}_t4.out"
    grep -q "SV cyc=129" "${tag}_t1.out"
    diff -u "${tag}_t1.out" "${tag}_t2.out"
    diff -u "${tag}_t1.out" "${tag}_t4.out"
    if command -v fst2vcd >/dev/null 2>&1; then
      fst2vcd -f "${tag}_t1.fst" | sed '/^\$date/,/^\$end/d' > "${tag}_t1.vcd"
      fst2vcd -f "${tag}_t2.fst" | sed '/^\$date/,/^\$end/d' > "${tag}_t2.vcd"
      fst2vcd -f "${tag}_t4.fst" | sed '/^\$date/,/^\$end/d' > "${tag}_t4.vcd"
      diff -u "${tag}_t1.vcd" "${tag}_t2.vcd"
      diff -u "${tag}_t1.vcd" "${tag}_t4.vcd"
    fi
  else
    nvc --work=work_sv -r --threads=1 tb_sv_cluster_mt > "${tag}_t1.out"
    nvc --work=work_sv -r --threads=4 tb_sv_cluster_mt > "${tag}_t4.out"
    grep -q "SV cyc=129" "${tag}_t1.out"
    diff -u "${tag}_t1.out" "${tag}_t4.out"
  fi
}

check_sv_mode module off 1
check_sv_mode module islands 0
check_sv_mode hier off 0
check_sv_mode hier islands 1

# --- Subtest 3: Multi-core VHDL RTL (32 hierarchical cores, >100 cycles) ---
cat > vhdl_cluster_mt.vhd <<'EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_leaf_mt is
  generic (SEED : natural := 1);
  port (
    clk   : in  std_logic;
    rst   : in  std_logic;
    din   : in  std_logic_vector(15 downto 0);
    dout  : out std_logic_vector(15 downto 0);
    comb  : out std_logic_vector(15 downto 0)
  );
end entity;

architecture rtl of vhdl_leaf_mt is
  signal acc  : unsigned(15 downto 0) := (others => '0');
  signal nxt  : unsigned(15 downto 0);
begin
  process(din, acc)
  begin
    nxt <= acc + unsigned(din) + to_unsigned(SEED, 16);
  end process;

  process(clk, rst)
  begin
    if rst = '1' then
      acc <= to_unsigned(SEED, 16);
    elsif rising_edge(clk) then
      acc <= nxt xor shift_left(acc, 1);
    end if;
  end process;

  dout <= std_logic_vector(acc);
  comb <= std_logic_vector(nxt);
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_core_mt is
  generic (CORE_ID : natural := 0);
  port (
    clk  : in  std_logic;
    rst  : in  std_logic;
    din  : in  std_logic_vector(15 downto 0);
    dout : out std_logic_vector(15 downto 0)
  );
end entity;

architecture rtl of vhdl_core_mt is
  signal s_q, s_c : std_logic_vector(15 downto 0);
begin
  u0: entity work.vhdl_leaf_mt
    generic map (SEED => CORE_ID * 7 + 3)
    port map (clk => clk, rst => rst, din => din, dout => s_q, comb => s_c);

  u1: entity work.vhdl_leaf_mt
    generic map (SEED => CORE_ID * 13 + 5)
    port map (clk => clk, rst => rst, din => s_c, dout => dout, comb => open);
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tb_vhdl_mt is
end entity;

architecture sim of tb_vhdl_mt is
  constant NC : natural := 32;
  type bus_arr_t is array (0 to NC - 1) of std_logic_vector(15 downto 0);
  signal clk  : std_logic := '0';
  signal rst  : std_logic := '1';
  signal din  : bus_arr_t := (others => (others => '0'));
  signal dout : bus_arr_t;
begin
  gen_cores: for i in 0 to NC - 1 generate
    u_core: entity work.vhdl_core_mt
      generic map (CORE_ID => i)
      port map (clk => clk, rst => rst, din => din(i), dout => dout(i));
  end generate;

  process
    variable xsum : unsigned(15 downto 0);
  begin
    wait for 5 ns;
    rst <= '0';
    for cyc in 0 to 129 loop
      for i in 0 to NC - 1 loop
        din(i) <= std_logic_vector(to_unsigned((cyc * 29 + i * 11 + 3) mod 65536, 16));
      end loop;
      wait for 5 ns;
      clk <= '1';
      wait for 5 ns;
      clk <= '0';
      if cyc < 4 or cyc = 64 or cyc = 129 then
        xsum := (others => '0');
        for i in 0 to NC - 1 loop
          xsum := xsum xor unsigned(dout(i));
        end loop;
        report "VHDL cyc=" & integer'image(cyc)
               & " d0=" & to_hstring(dout(0))
               & " dlast=" & to_hstring(dout(NC - 1))
               & " xsum=" & to_hstring(xsum);
      end if;
    end loop;
    wait;
  end process;
end architecture;
EOF

nvc --std=2008 --work=work_vhdl -a vhdl_cluster_mt.vhd

check_vhdl_mode() {
  local isl_mode=$1
  local two_mode=$2
  local full=$3
  local tag="vhdl_${isl_mode}_${two_mode}"
  nvc --std=2008 --work=work_vhdl --rtl-islands="$isl_mode" --2state="$two_mode" -e tb_vhdl_mt
  if [ "$full" = "1" ]; then
    nvc --std=2008 --work=work_vhdl -r --threads=1 --wave="${tag}_t1.fst" tb_vhdl_mt > "${tag}_t1.raw" 2>&1
    nvc --std=2008 --work=work_vhdl -r --threads=2 --wave="${tag}_t2.fst" tb_vhdl_mt > "${tag}_t2.raw" 2>&1
    nvc --std=2008 --work=work_vhdl -r --threads=4 --wave="${tag}_t4.fst" tb_vhdl_mt > "${tag}_t4.raw" 2>&1
    grep -E '^\*\* Note:.*VHDL cyc=' "${tag}_t1.raw" > "${tag}_t1.out"
    grep -E '^\*\* Note:.*VHDL cyc=' "${tag}_t2.raw" > "${tag}_t2.out"
    grep -E '^\*\* Note:.*VHDL cyc=' "${tag}_t4.raw" > "${tag}_t4.out"
    grep -q "VHDL cyc=129" "${tag}_t1.out"
    diff -u "${tag}_t1.out" "${tag}_t2.out"
    diff -u "${tag}_t1.out" "${tag}_t4.out"
    if command -v fst2vcd >/dev/null 2>&1; then
      fst2vcd -f "${tag}_t1.fst" | sed '/^\$date/,/^\$end/d' > "${tag}_t1.vcd"
      fst2vcd -f "${tag}_t2.fst" | sed '/^\$date/,/^\$end/d' > "${tag}_t2.vcd"
      fst2vcd -f "${tag}_t4.fst" | sed '/^\$date/,/^\$end/d' > "${tag}_t4.vcd"
      diff -u "${tag}_t1.vcd" "${tag}_t2.vcd"
      diff -u "${tag}_t1.vcd" "${tag}_t4.vcd"
    fi
  else
    nvc --std=2008 --work=work_vhdl -r --threads=1 tb_vhdl_mt > "${tag}_t1.raw" 2>&1
    nvc --std=2008 --work=work_vhdl -r --threads=4 tb_vhdl_mt > "${tag}_t4.raw" 2>&1
    grep -E '^\*\* Note:.*VHDL cyc=' "${tag}_t1.raw" > "${tag}_t1.out"
    grep -E '^\*\* Note:.*VHDL cyc=' "${tag}_t4.raw" > "${tag}_t4.out"
    grep -q "VHDL cyc=129" "${tag}_t1.out"
    diff -u "${tag}_t1.out" "${tag}_t4.out"
  fi
}

check_vhdl_mode module off 1
check_vhdl_mode module islands 0
check_vhdl_mode hier off 0
check_vhdl_mode hier islands 1

# --- Subtest 4: Multi-domain single-island test (two clocks firing at same timestamp) ---
cat > dual_dom_mt.sv <<'EOF'
module dual_dom_unit #(parameter int ID = 0) (
  input  logic       clk_a,
  input  logic       clk_b,
  input  logic       rst_n,
  input  logic [7:0] da,
  input  logic [7:0] db,
  output logic [7:0] qa,
  output logic [7:0] qb
);
  logic [7:0] mix_a, mix_b;
  always_comb begin
    mix_a = da + qb + ID[7:0];
    mix_b = db ^ qa ^ ID[7:0];
  end

  always_ff @(posedge clk_a or negedge rst_n) begin
    if (!rst_n) qa <= 8'h11;
    else        qa <= mix_a;
  end

  always_ff @(posedge clk_b or negedge rst_n) begin
    if (!rst_n) qb <= 8'h22;
    else        qb <= mix_b;
  end
endmodule

module tb_dual_dom_mt;
  localparam int N = 20;
  logic       clk_a = 1'b0, clk_b = 1'b0, rst_n = 1'b0;
  logic [7:0] da [0:N-1], db [0:N-1];
  logic [7:0] qa [0:N-1], qb [0:N-1];

  for (genvar i = 0; i < N; i++) begin : g_dd
    dual_dom_unit #(.ID(i)) u (
      .clk_a(clk_a), .clk_b(clk_b), .rst_n(rst_n),
      .da(da[i]), .db(db[i]), .qa(qa[i]), .qb(qb[i])
    );
  end

  initial begin
    for (int i = 0; i < N; i++) begin
      da[i] = 8'(i + 1);
      db[i] = 8'(i * 3 + 2);
    end
    #10 rst_n = 1'b1;
    for (int cyc = 0; cyc < 40; cyc++) begin
      #5;
      for (int i = 0; i < N; i++) begin
        da[i] = 8'((cyc * 7 + i) & 8'hff);
        db[i] = 8'((cyc * 13 + i * 5) & 8'hff);
      end
      // Fire BOTH clk_a and clk_b on the exact same timestamp!
      clk_a = 1'b1;
      clk_b = 1'b1;
      #5;
      clk_a = 1'b0;
      clk_b = 1'b0;
      if (cyc < 4 || cyc == 39)
        $display("DD cyc=%0d qa0=%02h qb0=%02h qalast=%02h qblast=%02h",
                 cyc, qa[0], qb[0], qa[N-1], qb[N-1]);
    end
    $finish;
  end
endmodule
EOF

nvc --work=work_dd -a dual_dom_mt.sv
nvc --work=work_dd --rtl-islands=module --2state=islands -e tb_dual_dom_mt
nvc --work=work_dd -r --threads=1 --wave=dd_t1.fst tb_dual_dom_mt > dd_t1.out
nvc --work=work_dd -r --threads=2 --wave=dd_t2.fst tb_dual_dom_mt > dd_t2.out
nvc --work=work_dd -r --threads=4 --wave=dd_t4.fst tb_dual_dom_mt > dd_t4.out
grep -q "DD cyc=39" dd_t1.out
diff -u dd_t1.out dd_t2.out
diff -u dd_t1.out dd_t4.out
if command -v fst2vcd >/dev/null 2>&1; then
  fst2vcd -f dd_t1.fst | sed '/^\$date/,/^\$end/d' > dd_t1.vcd
  fst2vcd -f dd_t2.fst | sed '/^\$date/,/^\$end/d' > dd_t2.vcd
  fst2vcd -f dd_t4.fst | sed '/^\$date/,/^\$end/d' > dd_t4.vcd
  diff -u dd_t1.vcd dd_t2.vcd
  diff -u dd_t1.vcd dd_t4.vcd
fi
