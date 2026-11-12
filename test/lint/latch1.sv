module latch1(input a, b, c, input [1:0] s,
              output reg y1, y2, y3, y4, y5, y6, y7, y8);
  typedef enum logic [1:0] {A, B, C} st_t;
  st_t st;
  always_comb begin
    if (a) y1 = b;                    // no else
  end
  always_comb begin
    y2 = 0;
    if (a) y2 = b;                    // default value first
  end
  always @* begin
    case (s)                          // not all values
      2'b00: y3 = a;
      2'b01: y3 = b;
    endcase
  end
  always_comb begin
    case (s)                          // all values
      2'b00: y4 = a;
      2'b01: y4 = b;
      2'b10: y4 = c;
      2'b11: y4 = 0;
    endcase
  end
  always_comb begin
    case (s)
      2'b00: y5 = a;
      default: y5 = b;
    endcase
  end
  always_comb begin
    case (st)                         // all names of the enumeration
      A: y6 = a;
      B: y6 = b;
      C: y6 = c;
    endcase
  end
  // nvc lint_off latch
  always_comb if (c) y7 = a;
  always_latch if (c) y8 = a;         // intended
  always @(posedge a) if (c) y8 <= b; // sequential
endmodule
