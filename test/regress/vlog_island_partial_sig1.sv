module isl_drv (
  input  logic clk,
  input  logic rst_n,
  input  logic d,
  output logic q
);
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) q <= 1'b0;
    else        q <= d;
  end
endmodule

module non_isl_drv (
  input  logic d,
  output logic q
);
  always_comb begin
    q = d;
    if (0) $display("eject");
  end
endmodule

module vlog_island_partial_sig1;
  logic clk = 0;
  logic rst_n = 1;
  logic [3:0] bus;
  logic [1:0] d_isl = 2'b10;
  logic [1:0] d_non = 2'b01;

  // Lower 2 bits owned by RTL islands; upper 2 bits driven by non-island procs
  isl_drv     u0 (.clk(clk), .rst_n(rst_n), .d(d_isl[0]), .q(bus[0]));
  isl_drv     u1 (.clk(clk), .rst_n(rst_n), .d(d_isl[1]), .q(bus[1]));
  non_isl_drv u2 (.d(d_non[0]), .q(bus[2]));
  non_isl_drv u3 (.d(d_non[1]), .q(bus[3]));

  initial begin
    #1;
    if (bus[3:2] !== 2'b01) $fatal(1, "init upper got %b", bus[3:2]);
    rst_n = 0;
    #1;
    if (bus !== 4'b0100) $fatal(1, "after rst got %b", bus);
    rst_n = 1;
    #1 clk = 1;
    #1 clk = 0;
    if (bus !== 4'b0110) $fatal(1, "after clk got %b", bus);
    d_non = 2'b10;
    #1;
    if (bus !== 4'b1010) $fatal(1, "after d_non got %b", bus);
    force bus = 4'b1111;
    #1;
    if (bus !== 4'b1111) $fatal(1, "after force got %b", bus);
    release bus;
    d_non = 2'b00;
    #1;
    if (bus[3:2] !== 2'b00) $fatal(1, "after release upper got %b", bus[3:2]);
    $display("PASSED");
    $finish;
  end
endmodule
