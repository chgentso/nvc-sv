#!/bin/bash -ex
set -xe
#
# Direct wires that close a cycle between SV islands (the islands feed each
# other through independent paths) stay direct, also when the clusters are
# evaluated on worker threads.
#

if [ -n "${ASAN_OPTIONS:-}" ]; then
  export ASAN_OPTIONS="$ASAN_OPTIONS:detect_leaks=0"
fi

cat > xcycle_mt.sv <<'EOF'
module xm_a #(parameter int SEED = 1)
  (input  logic [31:0] x_i, input logic [31:0] k_i,
   output logic [31:0] y_o, output logic [31:0] w_o);
  always_comb begin
    logic [31:0] h;
    h = x_i ^ SEED;
    for (int i = 0; i < 64; i++)
      h = {h[30:0], h[31]} ^ (h + 32'h9e3779b9);
    y_o = h;
  end
  always_comb begin
    logic [31:0] g;
    g = k_i;
    for (int i = 0; i < 64; i++)
      g = {g[0], g[31:1]} + (g ^ 32'h7f4a7c15);
    w_o = g;
  end
endmodule

module xm_b #(parameter int SEED = 1)
  (input  logic [31:0] cnt_i, input logic [31:0] y_i, input logic [31:0] w_i,
   output logic [31:0] x_o, output logic [31:0] k_o, output logic [31:0] z_o);
  assign x_o = cnt_i * SEED;
  assign k_o = cnt_i ^ SEED;
  assign z_o = y_i ^ w_i;
endmodule

module xcycle_mt;
  logic clk = 0;
  logic [31:0] cnt = 0;
  logic [31:0] acc = 0;
  logic [31:0] z [32];
  for (genvar g = 0; g < 32; g++) begin : gen
    logic [31:0] x, y, k, w;
    xm_a #(g + 1) ua (.x_i(x), .k_i(k), .y_o(y), .w_o(w));
    xm_b #(g * 7 + 3) ub (.cnt_i(cnt), .y_i(y), .w_i(w), .x_o(x), .k_o(k),
                          .z_o(z[g]));
  end
  always #5 clk = ~clk;
  always @(posedge clk) begin
    cnt <= cnt + 1;
    acc <= acc ^ z[0] ^ z[5] ^ z[17] ^ z[31] ^ {z[9][15:0], z[22][31:16]};
  end
  initial begin
    #20000;
    $display("acc=%h cnt=%0d", acc, cnt);
    $finish;
  end
endmodule
EOF

nvc -a xcycle_mt.sv
nvc --work=woff -a xcycle_mt.sv
nvc -e --rtl-islands=hier xcycle_mt
nvc --work=woff -e --rtl-islands=off xcycle_mt
nvc --work=woff -r xcycle_mt 2>&1 | grep -v '^\*\* Debug:' > off.out

NVC_ISLAND_WIRE_STATS=1 nvc -r --threads=1 xcycle_mt > t1.out 2>&1
grep -E '^ISLAND_WIRE: DIRECT +128 ' t1.out
! grep -E 'comb-cycle' t1.out
grep -v -e '^ISLAND_WIRE' -e '^\*\* Debug:' t1.out > t1.txt
diff -u off.out t1.txt

nvc -r --threads=4 xcycle_mt 2>&1 | grep -v '^\*\* Debug:' > t4.out
diff -u off.out t4.out
NVC_ISLAND_FAST=0 nvc -r --threads=4 xcycle_mt 2>&1 \
  | grep -v '^\*\* Debug:' > t4s.out
diff -u off.out t4s.out
grep 'acc=ae432d46 cnt=2000' off.out
