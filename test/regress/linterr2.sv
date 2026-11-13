module linterr2(input logic [1:0] s, input logic [3:0] d, output logic [1:0] p, output logic [1:0] q);
  always @* begin
    p = 2'd0;
    if (s == 2'd1)
      p = undefined_name;
  end
  always @* begin
    if (s == 2'd1)
      q = d;
  end
endmodule
