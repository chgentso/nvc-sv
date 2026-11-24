set -xe

cat >pure_rtl.sv <<'EOF'
module pure_rtl (
  input  logic        clk1,
  input  logic        clk2,
  input  logic        rst_n,
  input  logic [7:0]  a,
  input  bit   [7:0]  b,
  output logic [7:0]  y,
  output bit   [31:0] cnt_o
);
  logic [7:0]  comb1, comb2;
  logic [15:0] mem [0:3];
  bit   [31:0] cnt_q;

  assign comb1 = a + b;

  always_comb begin
    comb2 = comb1 ^ 8'h5a;
  end

  always_ff @(posedge clk1 or negedge rst_n) begin
    if (!rst_n) begin
      y <= 8'h00;
      for (int i = 0; i < 4; i++)
        mem[i] <= 16'h0000;
    end else begin
      y <= comb2;
      mem[a[1:0]] <= {comb1, comb2};
    end
  end

  always_ff @(negedge clk2) begin
    cnt_q <= cnt_q + 32'd1;
  end

  assign cnt_o = cnt_q;
endmodule

module tb_pure_rtl;
  logic        clk1 = 0, clk2 = 0, rst_n = 0;
  logic [7:0]  a = 8'h12;
  bit   [7:0]  b = 8'h34;
  logic [7:0]  y;
  bit   [31:0] cnt_o;

  pure_rtl dut (.*);

  initial begin
    #5 clk1 = 1; #5 clk1 = 0;
    rst_n = 1;
    #5 clk1 = 1; #5 clk1 = 0;
    #5 clk2 = 1; #5 clk2 = 0;
    if (y !== ((8'h12 + 8'h34) ^ 8'h5a)) begin
      $display("FAILED pure_rtl y=%h", y);
      $finish;
    end
    $display("PASSED pure_rtl");
    $finish;
  end
endmodule
EOF

cat >mixed_eject.sv <<'EOF'
module mixed_eject (
  input  logic clk,
  input  logic d,
  inout  wire  io_p,
  output logic q_o
);
  logic init_drv, obs_sig, dly_out;
  wire  md_wire;
  logic q_int;

  initial begin
    init_drv = 1'b1;
  end

  assign #2 dly_out = d;
  assign md_wire = d ? 1'b1 : 1'bz;
  assign md_wire = ~d ? 1'b0 : 1'bz;

  always_ff @(posedge clk) begin
    q_int <= d & init_drv & md_wire;
  end

  always_comb begin
    obs_sig = q_int ^ dly_out;
    q_o = obs_sig;
  end

  assert property (@(posedge clk) init_drv == 1'b1);
endmodule

module tb_mixed_eject;
  logic clk = 0, d = 1, q_o;
  wire io_p;

  mixed_eject dut (.clk(clk), .d(d), .io_p(io_p), .q_o(q_o));

  initial begin
    #5 clk = 1; #5 clk = 0;
    #5 clk = 1; #5 clk = 0;
    $display("PASSED mixed_eject");
    $finish;
  end
endmodule
EOF

cat >comb_cycle.sv <<'EOF'
module comb_cycle (
  input  logic en,
  output logic y
);
  logic a, b;
  assign a = b & en;
  assign b = ~a;
  assign y = a;
endmodule

module tb_comb_cycle;
  logic en = 0, y;
  comb_cycle dut (.en(en), .y(y));
  initial begin
    #1;
    $display("PASSED comb_cycle");
    $finish;
  end
endmodule
EOF

nvc-sv -a pure_rtl.sv -e --rtl-islands=module --dump-islands tb_pure_rtl -r > pure.out
grep -q "PASSED pure_rtl" pure.out
grep -q "ISLAND_MODULE pure_rtl (WORK.PURE_RTL#0): islands=1 procs=5/5 ejected_procs=0 ejected_sigs=0" pure.out
grep -q "assign=2 comb=1 ff=2 domains=2 topo_levels=2 comb_sccs=0" pure.out
grep -q "clk=clk1(posedge) rst=rst_n(negedge) ff=1" pure.out
grep -q "clk=clk2(negedge) rst=none ff=1" pure.out

nvc-sv -a mixed_eject.sv -e --rtl-islands=module --dump-islands tb_mixed_eject -r > mixed.out
grep -q "PASSED mixed_eject" mixed.out
grep -q "ISLAND_MODULE mixed_eject (WORK.MIXED_EJECT#0): islands=1 procs=2/" mixed.out
grep -q "EJECTED_PROCS: initial=1 delay=1 sva=" mixed.out
grep -q "multi_driver_target=2" mixed.out
grep -q "EJECTED_SIGS: multi_driver=1" mixed.out

nvc-sv -a comb_cycle.sv -e --rtl-islands=module --dump-islands tb_comb_cycle -r > cycle.out
grep -q "PASSED comb_cycle" cycle.out
grep -q "ISLAND_MODULE comb_cycle (WORK.COMB_CYCLE#0): islands=1 procs=3/3 ejected_procs=0 ejected_sigs=0" cycle.out
grep -q "comb_sccs=1" cycle.out
