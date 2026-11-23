module mixedext2_dut (
  input  logic        tick,
  output logic [31:0] iobs,
  output logic        bobs
);
  wire        w_bit = 1'b1;
  logic       l_bit = 1'b0;
  wire  [7:0] w_vec = 8'hA5;
  logic [7:0] l_vec = 8'h3C;
  logic signed [7:0] s_vec = -8'sd12;
  logic [7:0] u_vec = 8'd200;
  bit         b_bit = 1'b1;
  bit   [3:0] b_vec = 4'b1010;
  logic       flag  = 1'b1;
  int         i_var = -42;
  logic [7:0] n_var = 8'd77;
  real        r_var = 1.5;

  assign iobs = i_var;
  assign bobs = flag;

  always @(posedge tick) begin
    i_var = i_var + 1;
    flag  = ~flag;
    r_var = r_var + 0.75;
  end
endmodule
