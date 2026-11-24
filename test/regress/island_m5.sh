set -xe

# 1. Multi-level hierarchy (3+ nested SV modules sharing clock + async reset)
cat >hier_m5.sv <<'EOF'
module leaf_stage #(parameter int OFFSET = 1) (
  input  logic       clk,
  input  logic       rst_n,
  input  logic [7:0] din,
  output logic [7:0] comb_o,
  output logic [7:0] reg_o
);
  logic [7:0] c_tmp;
  always_comb c_tmp = din + OFFSET[7:0];
  assign comb_o = c_tmp ^ 8'h33;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)
      reg_o <= 8'h00;
    else
      reg_o <= c_tmp;
  end
endmodule

module mid_stage (
  input  logic       clk,
  input  logic       rst_n,
  input  logic [7:0] din,
  output logic [7:0] dout,
  output logic [7:0] comb_tap
);
  logic [7:0] s1_comb, s1_reg;
  logic [7:0] s2_comb, s2_reg;

  leaf_stage #(.OFFSET(3)) u_leaf1 (
    .clk    (clk),
    .rst_n  (rst_n),
    .din    (din),
    .comb_o (s1_comb),
    .reg_o  (s1_reg)
  );

  leaf_stage #(.OFFSET(7)) u_leaf2 (
    .clk    (clk),
    .rst_n  (rst_n),
    .din    (s1_comb ^ s1_reg),
    .comb_o (s2_comb),
    .reg_o  (s2_reg)
  );

  assign comb_tap = s2_comb;
  always_comb dout = s2_reg + s2_comb;
endmodule

module top_stage (
  input  logic       clk,
  input  logic       rst_n,
  input  logic [7:0] din,
  output logic [7:0] dout
);
  logic [7:0] m1_out, m1_tap;
  logic [7:0] m2_out, m2_tap;

  mid_stage u_m1 (.clk(clk), .rst_n(rst_n), .din(din), .dout(m1_out), .comb_tap(m1_tap));
  mid_stage u_m2 (.clk(clk), .rst_n(rst_n), .din(m1_out), .dout(m2_out), .comb_tap(m2_tap));

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)
      dout <= 8'h00;
    else
      dout <= m2_out ^ m1_tap ^ m2_tap;
  end
endmodule

module tb_hier_m5;
  logic       clk = 1'b0;
  logic       rst_n = 1'b0;
  logic [7:0] din = 8'h00;
  logic [7:0] dout;

  top_stage dut (.clk(clk), .rst_n(rst_n), .din(din), .dout(dout));

  always #5 clk = ~clk;

  initial begin
    #12 rst_n = 1'b1;
    for (int i = 0; i < 32; i++) begin
      @(negedge clk);
      din = (i * 17 + 5) & 8'hff;
      if (i == 15) begin
        #1 rst_n = 1'b0;
        #2 rst_n = 1'b1;
      end
      @(posedge clk);
      #1;
      $display("CYC %02d din=%02h dout=%02h", i, din, dout);
    end
    $display("PASSED hier_m5 dout=%02h", dout);
    $finish;
  end
endmodule
EOF

nvc-sv -a hier_m5.sv -e --rtl-islands=hier tb_hier_m5
nvc-sv -r --wave=hier_m5.fst tb_hier_m5 > hier_m5.out
grep -q "PASSED hier_m5" hier_m5.out

nvc-sv -a hier_m5.sv -e --rtl-islands=off tb_hier_m5
nvc-sv -r --wave=hier_off.fst tb_hier_m5 > hier_off.out
diff -u hier_off.out hier_m5.out
fst2vcd -f hier_m5.fst > hier_m5.vcd
fst2vcd -f hier_off.fst > hier_off.vcd
diff -u hier_off.vcd hier_m5.vcd

# 2. Instance-sharing verification across separate parameter-syntax AST sites
cat >shared_m5.sv <<'EOF'
module shared_unit #(
  parameter int W = 8,
  parameter int INIT = 0
) (
  input  logic         clk,
  input  logic         rst_n,
  input  logic [W-1:0] d,
  output logic [W-1:0] q
);
  logic [W-1:0] nxt;
  always_comb nxt = d + {{(W-1){1'b0}}, 1'b1};
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)
      q <= INIT[W-1:0];
    else
      q <= nxt;
  end
endmodule

module tb_shared_m5;
  logic       clk = 1'b0, rst_n = 1'b0;
  logic [7:0] d0, d1, d2, d3, d4, d5, d6, d7;
  logic [7:0] q0, q1, q2, q3, q4, q5, q6, q7;

  // Different AST param expressions that fold to identical W=8, INIT=0
  shared_unit #(.W(8), .INIT(0))       u0 (.clk(clk), .rst_n(rst_n), .d(d0), .q(q0));
  shared_unit #(8, 0)                  u1 (.clk(clk), .rst_n(rst_n), .d(d1), .q(q1));
  shared_unit #(.W(4+4), .INIT(0))     u2 (.clk(clk), .rst_n(rst_n), .d(d2), .q(q2));
  shared_unit #(.W(8), .INIT(1-1))     u3 (.clk(clk), .rst_n(rst_n), .d(d3), .q(q3));
  shared_unit #(.W(2*4), .INIT(0))     u4 (.clk(clk), .rst_n(rst_n), .d(d4), .q(q4));
  shared_unit #(.W(8), .INIT(32'sd0))  u5 (.clk(clk), .rst_n(rst_n), .d(d5), .q(q5));
  shared_unit #(.W(16/2), .INIT(0))    u6 (.clk(clk), .rst_n(rst_n), .d(d6), .q(q6));
  shared_unit #(.W(8))                 u7 (.clk(clk), .rst_n(rst_n), .d(d7), .q(q7));

  always #5 clk = ~clk;

  initial begin
    d0 = 8'h10; d1 = 8'h20; d2 = 8'h30; d3 = 8'h40;
    d4 = 8'h50; d5 = 8'h60; d6 = 8'h70; d7 = 8'h80;
    #12 rst_n = 1'b1;
    @(posedge clk);
    #1;
    $display("SHARED %02h %02h %02h %02h %02h %02h %02h %02h",
             q0, q1, q2, q3, q4, q5, q6, q7);
    if (q0 !== 8'h11 || q1 !== 8'h21 || q2 !== 8'h31 || q3 !== 8'h41 ||
        q4 !== 8'h51 || q5 !== 8'h61 || q6 !== 8'h71 || q7 !== 8'h81)
      $fatal(1, "independent instance state mismatch");
    $display("PASSED shared_m5");
    $finish;
  end
endmodule
EOF

NVC_LOWER_VERBOSE=island_shared nvc-sv -a shared_m5.sv -e --rtl-islands=hier tb_shared_m5 > shared_mir.log 2>&1
n_comb=$(grep -c "Name       WORK\.SHARED_UNIT\.\$island_shared_comb_" shared_mir.log || true)
n_ff=$(grep -c "Name       WORK\.SHARED_UNIT\.\$island_shared_ff_" shared_mir.log || true)
test "$n_comb" -eq 1
test "$n_ff" -eq 1

nvc-sv -r tb_shared_m5 > shared_hier.out
grep -q "PASSED shared_m5" shared_hier.out
nvc-sv -a shared_m5.sv -e --rtl-islands=off tb_shared_m5
nvc-sv -r tb_shared_m5 > shared_off.out
diff -u shared_off.out shared_hier.out

# 3. Combinational feedback across two child islands (safe back-edge fallback)
cat >cycle_m5.sv <<'EOF'
module cyc_a (
  input  logic       sel,
  input  logic [7:0] fb_in,
  input  logic [7:0] ext_in,
  output logic [7:0] to_b,
  output logic [7:0] obs_a
);
  always_comb begin
    to_b  = ext_in + 8'h01;
    obs_a = sel ? fb_in : (ext_in ^ 8'h55);
  end
endmodule

module cyc_b (
  input  logic [7:0] from_a,
  output logic [7:0] fb_out
);
  always_comb fb_out = from_a + 8'h02;
endmodule

module tb_cycle_m5;
  logic       sel = 1'b0;
  logic [7:0] ext_in = 8'h10;
  logic [7:0] w_ab, w_ba, obs_a;

  cyc_a u_a (.sel(sel), .fb_in(w_ba), .ext_in(ext_in), .to_b(w_ab), .obs_a(obs_a));
  cyc_b u_b (.from_a(w_ab), .fb_out(w_ba));

  initial begin
    #1;
    $display("CYC0 obs=%02h ba=%02h", obs_a, w_ba);
    sel = 1'b1; ext_in = 8'h20;
    #1;
    $display("CYC1 obs=%02h ba=%02h", obs_a, w_ba);
    sel = 1'b0; ext_in = 8'h30;
    #1;
    $display("CYC2 obs=%02h ba=%02h", obs_a, w_ba);
    $display("PASSED cycle_m5");
    $finish;
  end
endmodule
EOF

nvc-sv -a cycle_m5.sv -e --rtl-islands=hier tb_cycle_m5
nvc-sv -r tb_cycle_m5 > cycle_hier.out
grep -q "PASSED cycle_m5" cycle_hier.out
nvc-sv -a cycle_m5.sv -e --rtl-islands=off tb_cycle_m5
nvc-sv -r tb_cycle_m5 > cycle_off.out
diff -u cycle_off.out cycle_hier.out

# 4. SystemVerilog interface with tri-state driver resolved against RTL module
cat >iface_m5.sv <<'EOF'
interface bus_if(input logic clk, input logic rst_n, inout wire [1:0] tx);
  logic [1:0] tx_drv = 2'b00;
  bit active = 1'b0;
  logic seen;

  assign tx = active ? tx_drv : 2'bzz;
  always_comb seen = (tx[1] ^ tx[0]) === 1'b1;
endinterface

module rtl_tx(input logic clk, input logic rst_n, output logic [1:0] tx_o);
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) tx_o <= 2'b01;
    else        tx_o <= ~tx_o;
  end
endmodule

module tb_iface_m5;
  logic clk = 1'b0, rst_n = 1'b0;
  wire [1:0] tx;
  bus_if u_if(.clk(clk), .rst_n(rst_n), .tx(tx));
  rtl_tx u_dut(.clk(clk), .rst_n(rst_n), .tx_o(tx));

  always #5 clk = ~clk;

  initial begin
    #1;
    if (tx !== 2'b01 || u_if.seen !== 1'b1)
      $fatal(1, "iface tri-state mismatch tx=%b seen=%b", tx, u_if.seen);
    rst_n = 1'b1;
    @(posedge clk);
    #1;
    if (tx !== 2'b10 || u_if.seen !== 1'b1)
      $fatal(1, "iface post-edge mismatch tx=%b seen=%b", tx, u_if.seen);
    $display("PASSED iface_m5");
    $finish;
  end
endmodule
EOF

nvc-sv -a iface_m5.sv -e --rtl-islands=hier tb_iface_m5
nvc-sv -r tb_iface_m5 > iface_hier.out
grep -q "PASSED iface_m5" iface_hier.out
nvc-sv -a iface_m5.sv -e --rtl-islands=off tb_iface_m5
nvc-sv -r tb_iface_m5 > iface_off.out
diff -u iface_off.out iface_hier.out
