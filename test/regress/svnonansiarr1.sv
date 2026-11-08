// Non-ANSI interface array port declared before the interface is
// Expected values: IEEE 1800-2017 23.2.2.1 (non-ANSI port declarations), 25.3 and 25.5 (interface and interface
// array ports), 27.4 (generate loop assigning to an interface member): x[i].d = i+1 and y.d = 8'h5a
typedef struct { int n; int w; } svnonansiarr1_p;
module svnonansiarr1_sub(a, b);
  parameter svnonansiarr1_p P = '{3, 8};
  svnonansiarr1_if a [P.n];
  svnonansiarr1_if b;
  generate genvar i;
    for (i = 0; i < P.n; i++) begin
      assign a[i].d = i + 1;
    end
  endgenerate
  assign b.d = 8'h5a;
endmodule
interface svnonansiarr1_if;
  logic [7:0] d;
endinterface
module svnonansiarr1;
  svnonansiarr1_if x [3]();
  svnonansiarr1_if y();
  svnonansiarr1_sub s(x, y);
  initial begin
    #1;
    if (x[0].d !== 8'h01 || x[1].d !== 8'h02 || x[2].d !== 8'h03 || y.d !== 8'h5a)
      $error("%h %h %h %h", x[0].d, x[1].d, x[2].d, y.d);
    else
      $display("PASSED");
  end
endmodule
