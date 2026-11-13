module lintvlogrules2_sub(input a, input b, output y);
  assign y = a & b;
endmodule

module lintvlogrules2(input clk, input d, output y1, output y2);
  reg r, s, t;
  reg [1:0] v;
  lintvlogrules2_sub u1(.a(d), .b(), .y(y1));         // open input
  lintvlogrules2_sub u2(.a(d), .b(d), .y());          // open output is fine
  lintvlogrules2_sub u3(.a(d), .b(d), .y(y2));
  always @(posedge clk) r <= d;
  always @(posedge clk) r <= ~d;                      // r has two blocks
  always @(posedge clk) s <= d;
  always @(posedge clk) t <= d;
  always @(posedge clk) v[0] <= d;
  always @(posedge clk) v[1] <= d;                    // different bits
  initial s = 0;
endmodule
