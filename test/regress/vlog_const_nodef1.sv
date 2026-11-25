module child_nodef #(parameter width_p) (
  input  logic [width_p-1:0] a,
  output logic [width_p-1:0] y
);
  always_comb begin
    y = a[width_p-1:0];
  end
endmodule

module vlog_const_nodef1;
  logic [7:0] a, y;
  child_nodef #(.width_p(8)) u (.a(a), .y(y));

  initial begin
    a = 8'h5a;
    #1;
    if (y !== 8'h5a) $fatal(1, "expected 5a got %h", y);
    $display("PASSED");
    $finish;
  end
endmodule
