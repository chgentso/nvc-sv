module sub_wild #(parameter int W = 4) (
  input  logic [W-1:0] a,
  output logic [W-1:0] y
);
  assign y = a ^ {W{1'b1}};
endmodule

module mid_wild #(parameter int W = 4) (
  input  logic [W-1:0] a,
  output logic [W-1:0] y
);
  sub_wild #(.W(W)) u (.*);
endmodule

module gen_scan #(parameter int W = 4, parameter bit OR_P = 1) (
  input  logic [W-1:0] i,
  output logic [W-1:0] o
);
  wire [W-1:0] t [0:2];
  assign t[0] = i;
  genvar j;
  for (j = 0; j < 2; j = j + 1) begin : row
    wire [W-1:0] shifted = t[j] >> (1 << j);
    if (OR_P) begin : gen_or
      assign t[j+1] = t[j] | shifted;
    end
  end
  assign o = t[2];
endmodule

module vlog_gen_subblk1;
  logic [3:0] a, y_wild, y_scan;
  mid_wild #(.W(4)) uw (.a(a), .y(y_wild));
  gen_scan #(.W(4), .OR_P(1)) us (.i(a), .o(y_scan));

  initial begin
    a = 4'b1000;
    #1;
    if (y_wild !== 4'b0111) $fatal(1, "wild=%b", y_wild);
    if (y_scan !== 4'b1111) $fatal(1, "scan=%b", y_scan);
    $display("PASSED");
    $finish;
  end
endmodule
