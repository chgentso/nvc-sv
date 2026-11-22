typedef struct packed {
  logic a;
  logic b;
} pair_t;

interface svporthier3_if;
  pair_t sig;
endinterface

module svporthier3_sub (
  input  logic in_a,
  output logic out_y
);
  assign out_y = ~in_a;
endmodule

module svporthier3;
  svporthier3_if u_if();
  logic y;

  svporthier3_sub u_sub (
    .in_a  (u_if.sig.a),
    .out_y (y)
  );

  initial begin
    u_if.sig = '{a: 1'b1, b: 1'b0};
    #1;
    if (y !== 1'b0) $fatal(1, "case 1: y=%b", y);
    u_if.sig.a = 1'b0;
    #1;
    if (y !== 1'b1) $fatal(1, "case 2: y=%b", y);
    $display("PASSED");
    $finish;
  end
endmodule
