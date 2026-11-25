interface param_if #(parameter int VAL = 1);
  logic dummy;
endinterface

module sub_ifport (
  param_if pif,
  output logic [7:0] y
);
  assign y = pif.VAL;
endmodule

module vlog_island_ifport1;
  param_if #(17) if1();
  param_if #(42) if2();
  logic [7:0] y1, y2;

  sub_ifport u1 (.pif(if1), .y(y1));
  sub_ifport u2 (.pif(if2), .y(y2));

  initial begin
    #1;
    if (y1 !== 8'd17) $fatal(1, "y1=%0d exp 17", y1);
    if (y2 !== 8'd42) $fatal(1, "y2=%0d exp 42", y2);
    $display("PASSED");
  end
endmodule
