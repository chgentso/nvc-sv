// Self-checking. Expected values: IEEE 1800-2017 13.4 (a function may be
// called before its declaration in the module), 27.4 (a generate loop creates
// a block for each value of the genvar; names are looked up in the enclosing
// module), 10.3 (continuous assignment from a function call).
module svfwdfunc2 #(parameter int N = 3);
  logic [3:0] a = 4'd2;
  logic [3:0] y [N];
  int errs = 0;
  for (genvar i = 0; i < N; i++) begin : gen_s
    assign y[i] = bump(a) + i;
  end
  function automatic logic [3:0] bump(logic [3:0] x);
    return x + 1;
  endfunction
  initial begin
    #1;
    for (int k = 0; k < N; k++)
      if (y[k] !== 4'(3 + k)) begin errs++; $error("y[%0d] = %0d", k, y[k]); end
    a = 4'd5;
    #1 if (y[2] !== 4'd8) begin errs++; $error("y[2] = %0d", y[2]); end
    if (errs == 0) $display("PASSED");
  end
endmodule
