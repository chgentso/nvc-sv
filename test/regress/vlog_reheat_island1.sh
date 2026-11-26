set -xe

cat >reheat_island.sv <<'EOF'
module child_sink (
  input  logic       clk,
  input  logic       rst_n,
  input  logic [7:0] din,
  output logic [7:0] dout
);
  logic [7:0] q;
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)
      q <= 8'h00;
    else
      q <= din ^ 8'hA5;
  end
  assign dout = q;
endmodule

module child_src (
  input  logic       sel,
  output logic [7:0] y
);
  assign y = sel ? 8'h3C : 8'hC3;
endmodule

module child_io (
  input  logic       oe,
  input  logic [7:0] tx,
  inout  wire  [7:0] bus
);
  assign bus = oe ? tx : 8'hzz;
endmodule

module parent_mod (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       sel,
  input  logic       oe_parent,
  input  logic       oe_child,
  input  logic [7:0] a,
  input  logic [7:0] b,
  output logic [7:0] sink_out,
  output logic [7:0] sum_out,
  output logic [7:0] bus_obs
);
  // Driven only in parent_mod, connected only to child_sink's input port:
  // must fuse into parent_mod's island during both -e and separate -r reheat.
  logic [7:0] to_sink_comb;
  logic [7:0] to_sink_reg;
  logic [7:0] from_src;
  wire  [7:0] io_bus;

  always_comb begin
    to_sink_comb = a + b;
  end

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)
      to_sink_reg <= 8'h00;
    else
      to_sink_reg <= to_sink_comb;
  end

  child_sink u_sink (
    .clk  (clk),
    .rst_n(rst_n),
    .din  (to_sink_reg),
    .dout (sink_out)
  );

  // Driven by child_src's output port: must be ext_written in parent_mod.
  child_src u_src (
    .sel(sel),
    .y  (from_src)
  );

  assign sum_out = from_src + to_sink_comb;

  // Connected to child_io's inout port AND driven in parent_mod:
  // must stay multi-driven / ext_written and resolve properly.
  assign io_bus = oe_parent ? (a ^ 8'hFF) : 8'hzz;

  child_io u_io (
    .oe (oe_child),
    .tx (b),
    .bus(io_bus)
  );

  assign bus_obs = io_bus;
endmodule

module tb_reheat_island;
  logic       clk = 0, rst_n = 0, sel = 0, oe_parent = 0, oe_child = 0;
  logic [7:0] a = 8'h12, b = 8'h34;
  logic [7:0] sink_out, sum_out, bus_obs;

  parent_mod dut (.*);

  task automatic tick;
    #5 clk = 1;
    #5 clk = 0;
  endtask

  initial begin
    #1;
    tick();
    rst_n = 1;
    oe_parent = 1;
    oe_child = 0;
    #1;
    if (sum_out !== (8'hC3 + 8'h46))
      $fatal(1, "sum_out1=%h", sum_out);
    if (bus_obs !== (8'h12 ^ 8'hFF))
      $fatal(1, "bus_obs1=%h", bus_obs);

    tick(); // to_sink_reg <= 0x46, sink_out <= 0x00 ^ 0xA5
    #1;
    if (sink_out !== 8'hA5)
      $fatal(1, "sink_out1=%h", sink_out);

    sel = 1;
    a = 8'h50;
    b = 8'h25;
    oe_parent = 0;
    oe_child = 1;
    tick(); // to_sink_reg <= 0x75, sink_out <= 0x46 ^ 0xA5 = 0xE3
    #1;
    if (sink_out !== (8'h46 ^ 8'hA5))
      $fatal(1, "sink_out2=%h", sink_out);
    if (sum_out !== (8'h3C + 8'h75))
      $fatal(1, "sum_out2=%h", sum_out);
    if (bus_obs !== 8'h25)
      $fatal(1, "bus_obs2=%h", bus_obs);

    tick(); // sink_out <= 0x75 ^ 0xA5 = 0xD0
    #1;
    if (sink_out !== (8'h75 ^ 8'hA5))
      $fatal(1, "sink_out3=%h", sink_out);

    $display("PASSED reheat_island sink=%h sum=%h bus=%h",
             sink_out, sum_out, bus_obs);
    $finish;
  end
endmodule
EOF

# Separate -e and -r invocations so reheat.c lowers all Verilog instances
nvc-sv -a reheat_island.sv
nvc-sv -e --rtl-islands=hier tb_reheat_island
NVC_DUMP_ISLANDS=1 nvc-sv -r tb_reheat_island > run_hier.out 2>&1
grep -q "PASSED reheat_island" run_hier.out

# Verify parent_mod fused its drivers connected to child_sink's input port during -r
# (without find_mod in reheat, parent_mod's always_ff is ejected giving direct_wires=1 super_domains=0)
grep -q "ISLAND_HIER: islands=3 super_islands=1 direct_wires=2 super_domains=1" run_hier.out

# Also check --island-fast and --rtl-islands=off produce identical output
nvc-sv -r --island-fast tb_reheat_island > run_fast.out
grep -q "PASSED reheat_island" run_fast.out

nvc-sv -e --rtl-islands=off tb_reheat_island
nvc-sv -r tb_reheat_island > run_off.out
grep -q "PASSED reheat_island" run_off.out

grep "PASSED reheat_island" run_off.out > res_off.txt
grep "PASSED reheat_island" run_fast.out > res_fast.txt
grep "PASSED reheat_island" run_hier.out > res_hier.txt
diff -u res_off.txt res_fast.txt
diff -u res_off.txt res_hier.txt
