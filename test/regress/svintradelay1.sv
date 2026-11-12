// Intra-assignment delay on a bit select
module svintradelay1;
  logic [3:0] x;
  integer i = 2;
  logic [3:0] a[0:1];

  initial begin
    x = 0;
    x[0] = #1 1;
    if (x !== 4'b0001) $fatal(1, "x[0] = %b", x);
    x[i] = #1 1;
    if (x !== 4'b0101) $fatal(1, "x[i] = %b", x);
    a[0] = 0;
    a[0][1] = #1 1;
    if (a[0] !== 4'b0010) $fatal(1, "a[0][1] = %b", a[0]);
    $display("PASSED");
  end
endmodule
