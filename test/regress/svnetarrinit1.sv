// A net array initialised in its declaration
module svnetarrinit1;
  wire [3:0] a[0:1] = '{4'h1, 4'h2};
  wire b[0:2] = '{1'b1, 1'b0, 1'b1};

  initial begin
    #1;
    if (a[0] !== 4'h1 || a[1] !== 4'h2) $fatal(1, "a = %h %h", a[0], a[1]);
    if (b[0] !== 1'b1 || b[1] !== 1'b0 || b[2] !== 1'b1)
      $fatal(1, "b = %b %b %b", b[0], b[1], b[2]);
    $display("PASSED");
  end
endmodule
