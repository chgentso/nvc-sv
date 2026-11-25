// Test wide packed SV variables above the SIG_F_PACKED width cap (>256b 1D
// and >128b multi-dim) falling back cleanly to multi-nexus storage across
// combinational cascades, NBAs, and hierarchical references.
module vlog_packed3_sub(input logic [63:0] seed_i, output logic [63:0] out_o);
  logic [15:0][63:0] stage;
  logic [511:0]      wide1d = '0;

  assign stage[0] = seed_i;

  for (genvar r = 0; r < 15; r++) begin : gen_r
    always_comb begin
      stage[r + 1] = (stage[r] ^ (64'h0123_4567_89ab_cdef + r))
                     + {stage[r][62:0], stage[r][63]};
    end
  end

  assign out_o = stage[15];

  always_comb begin
    wide1d[63:0]    = seed_i;
    wide1d[511:448] = out_o;
  end
endmodule

module vlog_packed3;
  logic [63:0] seed = 64'hdead_beef_cafe_babe;
  logic [63:0] out_w;
  logic [15:0][63:0] reg_arr;
  logic clk = 1'b0;

  vlog_packed3_sub u_sub(.seed_i(seed), .out_o(out_w));

  always @(posedge clk) begin
    reg_arr[0]  <= seed;
    reg_arr[15] <= out_w;
  end

  initial begin
    #1;
    if (u_sub.wide1d[63:0] !== seed || u_sub.wide1d[511:448] !== out_w)
      $fatal(1, "wide1d mismatch: lo=%h hi=%h out=%h",
             u_sub.wide1d[63:0], u_sub.wide1d[511:448], out_w);
    if (u_sub.stage[15] !== out_w)
      $fatal(1, "hier stage mismatch");

    clk = 1'b1; #1; clk = 1'b0; #1;
    if (reg_arr[0] !== seed || reg_arr[15] !== out_w)
      $fatal(1, "reg_arr mismatch: [0]=%h [15]=%h", reg_arr[0], reg_arr[15]);

    seed = 64'h1122_3344_5566_7788;
    #1;
    if (u_sub.wide1d[63:0] !== seed || u_sub.wide1d[511:448] !== out_w)
      $fatal(1, "updated wide1d mismatch");

    clk = 1'b1; #1; clk = 1'b0; #1;
    if (reg_arr[0] !== seed || reg_arr[15] !== out_w)
      $fatal(1, "updated reg_arr mismatch");

    $display("PASSED");
    $finish;
  end
endmodule
