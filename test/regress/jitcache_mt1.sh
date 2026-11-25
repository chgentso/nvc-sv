#!/bin/bash -ex
set -xe

if [ -n "${ASAN_OPTIONS:-}" ]; then
  export ASAN_OPTIONS="$ASAN_OPTIONS:detect_leaks=0"
fi

# --- Design 1: Multi-module SV with distinct parameterized units + duplicates ---
cat > sv_mt_cache.sv <<'EOF'
module sv_lane #(parameter int ID = 0, parameter int SHIFT = 1) (
  input  logic        clk,
  input  logic [15:0] din,
  output logic [15:0] dout
);
  logic [15:0] acc, comb;

  function automatic logic [15:0] mix(input logic [15:0] a, input logic [15:0] b);
    logic [15:0] t;
    t = ((a << SHIFT) ^ (b >> (SHIFT & 3))) + 16'(ID * 13 + 7);
    return t ^ {t[7:0], t[15:8]};
  endfunction

  always_comb begin
    comb = mix(acc, din) ^ 16'(ID * 37 + 5);
  end

  always_ff @(posedge clk) begin
    acc <= comb + 16'(ID + 1);
  end

  assign dout = acc ^ comb;
endmodule

module tb_sv_mt_cache;
  localparam int N = 12;
  logic        clk = 1'b0;
  logic [15:0] din = 16'h0001;
  logic [15:0] dout [0:N-1];

  // 8 distinct parameterizations + 4 duplicates sharing identical keys
  sv_lane #(1, 1) u0  (.clk(clk), .din(din), .dout(dout[0]));
  sv_lane #(2, 2) u1  (.clk(clk), .din(din), .dout(dout[1]));
  sv_lane #(3, 3) u2  (.clk(clk), .din(din), .dout(dout[2]));
  sv_lane #(4, 1) u3  (.clk(clk), .din(din), .dout(dout[3]));
  sv_lane #(5, 2) u4  (.clk(clk), .din(din), .dout(dout[4]));
  sv_lane #(6, 3) u5  (.clk(clk), .din(din), .dout(dout[5]));
  sv_lane #(7, 1) u6  (.clk(clk), .din(din), .dout(dout[6]));
  sv_lane #(8, 2) u7  (.clk(clk), .din(din), .dout(dout[7]));
  sv_lane #(1, 1) u8  (.clk(clk), .din(din), .dout(dout[8]));
  sv_lane #(2, 2) u9  (.clk(clk), .din(din), .dout(dout[9]));
  sv_lane #(3, 3) u10 (.clk(clk), .din(din), .dout(dout[10]));
  sv_lane #(4, 1) u11 (.clk(clk), .din(din), .dout(dout[11]));

  always #5 clk = ~clk;

  initial begin
    int cycles;
    logic [15:0] xsum;
    cycles = 400;
    if (!$value$plusargs("CYCLES=%d", cycles))
      cycles = 400;

    u0.acc  = 16'h1001; u1.acc  = 16'h1002; u2.acc  = 16'h1003; u3.acc  = 16'h1004;
    u4.acc  = 16'h1005; u5.acc  = 16'h1006; u6.acc  = 16'h1007; u7.acc  = 16'h1008;
    u8.acc  = 16'h1001; u9.acc  = 16'h1002; u10.acc = 16'h1003; u11.acc = 16'h1004;

    for (int c = 0; c < cycles; c++) begin
      @(negedge clk);
      din = 16'((din * 16'd251 + 16'd17) & 16'hffff);
      @(posedge clk);
      #1;
    end

    if (dout[0] !== dout[8] || dout[1] !== dout[9]
        || dout[2] !== dout[10] || dout[3] !== dout[11]) begin
      $display("FAILED dup mismatch");
      $finish;
    end

    xsum = 16'h0000;
    for (int i = 0; i < N; i++)
      xsum ^= dout[i];
    $display("SV_OK cycles=%0d xsum=%04h d0=%04h d7=%04h",
             cycles, xsum, dout[0], dout[7]);
    $finish;
  end
endmodule
EOF

# --- Design 2: Multi-unit VHDL with distinct generics ---
cat > vhdl_mt_cache.vhd <<'EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_lane is
  generic (
    SEED : natural := 1
  );
  port (
    clk  : in  std_ulogic;
    din  : in  unsigned(15 downto 0);
    dout : out unsigned(15 downto 0)
  );
end entity;

architecture rtl of vhdl_lane is
  signal acc : unsigned(15 downto 0) := to_unsigned(SEED * 7, 16);
begin
  process(clk)
    variable tmp : unsigned(15 downto 0);
  begin
    if rising_edge(clk) then
      tmp := (acc(14 downto 0) & acc(15)) xor (din + to_unsigned(SEED * 19 + 3, 16));
      acc <= tmp + to_unsigned(SEED, 16);
    end if;
  end process;
  dout <= acc;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tb_vhdl_mt_cache is
end entity;

architecture sim of tb_vhdl_mt_cache is
  constant N : natural := 8;
  type u16_arr_t is array (0 to N - 1) of unsigned(15 downto 0);
  signal clk  : std_ulogic := '0';
  signal din  : unsigned(15 downto 0) := x"0003";
  signal dout : u16_arr_t;
  signal done : boolean := false;
begin
  g_lanes : for i in 0 to N - 1 generate
    u_lane : entity work.vhdl_lane
      generic map (SEED => i + 1)
      port map (clk => clk, din => din, dout => dout(i));
  end generate;

  clk <= not clk after 5 ns when not done else '0';

  process
    variable xsum : unsigned(15 downto 0) := (others => '0');
  begin
    for c in 0 to 300 loop
      wait until falling_edge(clk);
      din <= resize(din * 13 + 5, 16);
      wait until rising_edge(clk);
      wait for 1 ns;
    end loop;

    for i in 0 to N - 1 loop
      xsum := xsum xor dout(i);
    end loop;
    report "VHDL_OK xsum=" & integer'image(to_integer(xsum));
    done <= true;
    wait;
  end process;
end architecture;
EOF

nvc-sv --work=work_sv -a sv_mt_cache.sv
nvc-sv --work=work_sv --rtl-islands=hier -e tb_sv_mt_cache

nvc-sv --work=work_vhdl -a vhdl_mt_cache.vhd
nvc-sv --work=work_vhdl --rtl-islands=hier -e tb_vhdl_mt_cache

export NVC_JIT_THRESHOLD=4
export NVC_JIT_ASYNC=1
export NVC_JIT_LOG=1

# 1. Cold runs into fresh isolated cache dirs under --threads=1, 2, 4
for T in 1 2 4; do
  CDIR="$(pwd)/jcache_t${T}"
  rm -rf "$CDIR"

  NVC_JIT_CACHE_DIR="$CDIR" nvc-sv --work=work_sv --threads="$T" -r tb_sv_mt_cache \
    > "sv_cold_t${T}.out" 2> "sv_cold_t${T}.log"
  grep -q "SV_OK cycles=400" "sv_cold_t${T}.out"

  NVC_JIT_CACHE_DIR="$CDIR" nvc-sv --work=work_vhdl --threads="$T" -r tb_vhdl_mt_cache \
    > "vhdl_cold_t${T}.out" 2> "vhdl_cold_t${T}.log"
  grep -q "VHDL_OK xsum=" "vhdl_cold_t${T}.log"

  # Cache entries must be populated and zero stray .tmp.* files left behind
  test "$(find "$CDIR" -name '*.o' | wc -l)" -gt 0
  test "$(find "$CDIR" -name '.tmp.*' | wc -l)" -eq 0
done

# Bit-exact cold parity across 1, 2, 4 threads
diff -u sv_cold_t1.out sv_cold_t2.out
diff -u sv_cold_t1.out sv_cold_t4.out
grep "VHDL_OK" vhdl_cold_t1.log > vhdl_t1.res
grep "VHDL_OK" vhdl_cold_t2.log > vhdl_t2.res
grep "VHDL_OK" vhdl_cold_t4.log > vhdl_t4.res
diff -u vhdl_t1.res vhdl_t2.res
diff -u vhdl_t1.res vhdl_t4.res

# 2. Warm runs against the --threads=4 populated cache (sync check guarantees all compiled units hit cache)
WARM_DIR="$(pwd)/jcache_t4"
for T in 1 2 4; do
  NVC_JIT_CACHE_DIR="$WARM_DIR" nvc-sv --work=work_sv --threads="$T" -r tb_sv_mt_cache \
    > "sv_warm_t${T}.out" 2> "sv_warm_t${T}.log"
  diff -u sv_cold_t1.out "sv_warm_t${T}.out"
  grep -q "\[cache hit\]" "sv_warm_t${T}.log"

  NVC_JIT_CACHE_DIR="$WARM_DIR" nvc-sv --work=work_vhdl --threads="$T" -r tb_vhdl_mt_cache \
    > "vhdl_warm_t${T}.out" 2> "vhdl_warm_t${T}.log"
  grep "VHDL_OK" "vhdl_warm_t${T}.log" > "vhdl_warm_t${T}.res"
  diff -u vhdl_t1.res "vhdl_warm_t${T}.res"
  grep -q "\[cache hit\]" "vhdl_warm_t${T}.log"
done

# 3. Rapid short-run shutdown stress under --threads=4 with threshold=1 (in-flight drain + discard)
SHORT_DIR="$(pwd)/jcache_short"
for ITER in 1 2 3; do
  rm -rf "$SHORT_DIR"
  NVC_JIT_THRESHOLD=1 NVC_JIT_CACHE_DIR="$SHORT_DIR" \
    nvc-sv --work=work_sv --threads=4 -r tb_sv_mt_cache +CYCLES=2 \
    > "sv_short_${ITER}.out" 2> "sv_short_${ITER}.log"
  grep -q "SV_OK cycles=2" "sv_short_${ITER}.out"
  test "$(find "$SHORT_DIR" -name '.tmp.*' 2>/dev/null | wc -l)" -eq 0
done
diff -u sv_short_1.out sv_short_2.out
diff -u sv_short_1.out sv_short_3.out

rm -rf jcache_t1 jcache_t2 jcache_t4 jcache_short
