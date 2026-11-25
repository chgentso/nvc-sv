module hop_buf (
  input  logic a,
  output logic y
);
  assign y = a;
endmodule

module vlog_island_reset_hop1;
  logic src = 1'b1;
  logic n1, n2, n3;

  hop_buf u1 (.a(src), .y(n1));
  hop_buf u2 (.a(n1),  .y(n2));
  hop_buf u3 (.a(n2),  .y(n3));

  initial begin
    if (n3 !== 1'b1) $fatal(1, "t=0 n3=%b exp 1", n3);
    $display("PASSED");
  end
endmodule
