// Self-checking. Expected values: IEEE 1800-2017 23.3.3.2 (an output port
// of a module drives the actual connected to it through a continuous
// assignment, which can be a concatenation of variables given by hierarchical
// names, here members of an interface instance) and 6.5 (a variable can be
// written by one continuous assignment; the parts of the concatenation are
// different variables).
interface svporthier1_if(input logic clk);
  logic ack;
  logic [3:0] d_data;
endinterface

module svporthier1_sub(output logic [4:0] o);
  assign o = 5'b10110;
endmodule

module svporthier1;
  svporthier1_if ci(.clk(1'b0));
  svporthier1_sub u(.o({ci.ack, ci.d_data[3:0]}));
  int errs = 0;
  initial begin
    #1;
    if (ci.ack !== 1'b1 || ci.d_data !== 4'b0110) begin
      errs++; $error("ack=%b d_data=%b", ci.ack, ci.d_data);
    end
    if (errs == 0) $display("PASSED");
  end
endmodule
