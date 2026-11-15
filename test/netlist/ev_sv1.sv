// Clock evidence of Verilog processes
module sync2 #(parameter W = 1) (input logic clk, input logic [W-1:0] d,
                                 output logic [W-1:0] q);
  (* ASYNC_REG = "TRUE" *) logic [W-1:0] s0, s1;
  always_ff @(posedge clk) begin
    s0 <= d;
    s1 <= s0;
  end
  assign q = s1;
endmodule

module sync2_plain (input logic clk, input logic d, output logic q);
  logic s0, s1;
  always_ff @(posedge clk) begin
    s0 <= d;
    s1 <= s0;
  end
  assign q = s1;
endmodule

(* keep_hierarchy = "yes", cdc_group = "g1" *)
module ev_sv1;
  logic clk_a, clk_b, rst_n, rst_set, srst, en, sel;
  logic [3:0] din, dout; logic o1, o2, o3, o4;
  logic [3:0] ra, rb;
  logic t_a, tmp, gclk, gen_en;
  (* async_reg *) reg meta;
  reg a1, a2, a3, a4, a5;
  reg [3:0] mem [0:3];
  reg lat;

  // Asynchronous reset to zero, low active, with enable
  always_ff @(posedge clk_a or negedge rst_n)
    if (!rst_n) begin ra <= '0; t_a <= 0; end
    else begin ra <= din; if (en) t_a <= ~t_a; end

  // Parenthesised event, set to one, high active
  always @(posedge(clk_b), posedge(rst_set))
    if (rst_set) a1 <= 1'b1;
    else a1 <= ra[0];

  // Comma list and "or" list, falling edge
  always @(negedge clk_b or negedge rst_n)
    if (rst_n == 1'b0) a2 <= 0; else a2 <= a1;

  // iff
  always_ff @(posedge clk_b iff en) a3 <= din[1] & sel;

  // Synchronous reset
  always_ff @(posedge clk_b) begin
    if (srst) a4 <= 1'b0;
    else a4 <= a3;
  end

  // Blocking temporary
  always_ff @(posedge clk_b) begin
    tmp = a3 ^ a4;
    a5 <= tmp;
  end

  // Both edges, copy of a slice
  always @(edge clk_a) o1 <= ra[2];

  // Two clocks in one process
  always @(posedge clk_a, posedge clk_b) o2 <= din[0];

  // Level sensitive
  always_comb o3 = sel ? a1 : a2;
  always @* o4 = a4 & en;
  always_latch if (en) lat = din[3];

  always @(posedge clk_b) mem[din[1:0]] <= din;

  assign gclk = clk_a & gen_en;
  always @(posedge clk_b) gen_en <= din[0];
  assign dout = rb;
  buf (meta, ra[1]);

  genvar i;
  generate for (i = 0; i < 4; i++) begin : g
    sync2 u (.clk(clk_b), .d(ra[i]), .q(rb[i]));
  end endgenerate
  sync2_plain up (.clk(clk_b), .d(t_a), .q());

  logic [1:0] ux;
  sync2_plain ua [1:0] (.clk(clk_a), .d(din[1:0]), .q(ux));
endmodule
