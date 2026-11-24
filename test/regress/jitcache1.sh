set -xe

CDIR="$(pwd)/jcache"
rm -rf "$CDIR"

cat >worker.sv <<'EOF'
module worker #(parameter int ID = 0) (
  input logic clk,
  input logic [7:0] din,
  output logic [15:0] dout
);
  logic [15:0] acc;
  function automatic logic [15:0] mix(input logic [15:0] a, input logic [7:0] b);
    return ((a << 3) ^ {8'h00, b}) + 16'd7;
  endfunction

  always_ff @(posedge clk) begin
    acc <= mix(acc, din) + 16'(ID);
  end
  assign dout = acc;
endmodule

module top;
  logic clk = 0;
  logic [7:0] din = 8'd1;
  logic [15:0] d0, d1, d2, d3;

  worker #(0) u0 (.clk(clk), .din(din), .dout(d0));
  worker #(0) u1 (.clk(clk), .din(din), .dout(d1));
  worker #(0) u2 (.clk(clk), .din(din), .dout(d2));
  worker #(0) u3 (.clk(clk), .din(din), .dout(d3));

  initial begin
    u0.acc = 16'd10;
    u1.acc = 16'd10;
    u2.acc = 16'd10;
    u3.acc = 16'd10;
    for (int i = 0; i < 20; i++) begin
      #5 clk = ~clk;
      din = din + 8'd3;
    end
    if (d0 !== d1 || d1 !== d2 || d2 !== d3) begin
      $display("FAILED mismatch %h %h %h %h", d0, d1, d2, d3);
      $finish;
    end
    $display("PASSED sv=%h", d0);
    $finish;
  end
endmodule
EOF

cat >vhdl_top.vhd <<'EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_top is
end entity;

architecture beh of vhdl_top is
  function fib(n : natural) return natural is
  begin
    if n <= 1 then
      return n;
    else
      return fib(n - 1) + fib(n - 2);
    end if;
  end function;
begin
  process
    variable r : natural := 0;
  begin
    for i in 1 to 10 loop
      r := r + fib(i);
    end loop;
    assert r = 143 report "unexpected fib sum" severity failure;
    report "PASSED vhdl=" & integer'image(r);
    wait;
  end process;
end architecture;
EOF

nvc-sv -a worker.sv -e top
nvc-sv -a vhdl_top.vhd -e vhdl_top

export NVC_JIT_THRESHOLD=1
export NVC_JIT_ASYNC=0
export NVC_JIT_LOG=1
export NVC_JIT_CACHE_DIR="$CDIR"

# 1. Cold run populates cache
nvc-sv -r top > sv_cold.out 2> sv_cold.log
grep -q "PASSED sv=" sv_cold.out
grep -q "\[cache miss\]" sv_cold.log

nvc-sv -r vhdl_top > vhdl_cold.out 2> vhdl_cold.log
grep -q "PASSED vhdl=143" vhdl_cold.log
grep -q "\[cache miss\]" vhdl_cold.log

# 2. Warm run gets 100% cache hits and identical output
nvc-sv -r top > sv_warm.out 2> sv_warm.log
diff -u sv_cold.out sv_warm.out
grep -q "\[cache hit\]" sv_warm.log
if grep -q "\[cache miss\]" sv_warm.log; then
  echo "unexpected cache miss on warm SV run"
  exit 1
fi

nvc-sv -r vhdl_top > vhdl_warm.out 2> vhdl_warm.log
grep -q "PASSED vhdl=143" vhdl_warm.log
grep -q "\[cache hit\]" vhdl_warm.log
if grep -q "\[cache miss\]" vhdl_warm.log; then
  echo "unexpected cache miss on warm VHDL run"
  exit 1
fi

# 3. Corrupt a cached object file and verify fallback
FIRST_OBJ=$(find "$CDIR" -name '*.o' | head -n 1)
test -n "$FIRST_OBJ"
printf 'CORRUPT' > "$FIRST_OBJ"
nvc-sv -r top > sv_corrupt.out 2> sv_corrupt.log
diff -u sv_cold.out sv_corrupt.out

# 4. --jit-cache=clear removes cached .o files
nvc-sv --jit-cache-dir="$CDIR" --jit-cache=clear
test "$(find "$CDIR" -name '*.o' | wc -l)" -eq 0

# 5. --jit-cache=off and NVC_JIT_CACHE=0 bypass cache completely
nvc-sv --jit-cache=off -r top > sv_off.out 2> sv_off.log
diff -u sv_cold.out sv_off.out
test "$(find "$CDIR" -name '*.o' | wc -l)" -eq 0

NVC_JIT_CACHE=0 nvc-sv -r vhdl_top > vhdl_off.out 2> vhdl_off.log
grep -q "PASSED vhdl=143" vhdl_off.log
test "$(find "$CDIR" -name '*.o' | wc -l)" -eq 0

rm -rf "$CDIR"
