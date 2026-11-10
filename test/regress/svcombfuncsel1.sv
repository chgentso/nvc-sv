// Self-checking. Expected values: IEEE 1800-2017 9.2.2.2.1 (always_comb is
// sensitive to the expressions read in the block and in functions it calls,
// but not to variables declared inside those functions: their formal
// arguments and locals, also when selected with a constant index); 13.4 (the
// function value is the value returned).
package svcombfuncsel1_pkg;
  function automatic logic [7:0] mul2(logic [7:0] in);
    logic [7:0] out;
    out[7] = in[6]; out[6] = in[5]; out[5] = in[4]; out[4] = in[3] ^ in[7];
    out[3] = in[2] ^ in[7]; out[2] = in[1]; out[1] = in[0] ^ in[7]; out[0] = in[7];
    return out;
  endfunction
endpackage

module svcombfuncsel1;
  import svcombfuncsel1_pkg::*;
  logic [7:0] q = 8'h57, d;
  int errs = 0;
  always_comb d = mul2(q);
  initial begin
    #1 if (d !== 8'hae) begin errs++; $error("d = %h expected ae", d); end
    q = 8'h80;
    #1 if (d !== 8'h1b) begin errs++; $error("d = %h expected 1b", d); end
    if (errs == 0) $display("PASSED");
  end
endmodule
