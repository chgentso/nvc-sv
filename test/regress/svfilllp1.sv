// Self-checking. IEEE 1800-2017 5.7.1: an unsized '1 or 'z value fills the
// width of the parameter it initialises, also when the type has a range but
// no data type keyword, and inside a function.
module svfilllp1_sub #(parameter int W = 1) (output bit [15:0] o);
  localparam [3:0] N = '1;
  localparam [W-1:0] M = '1;
  parameter [3:0] P = '1;
  function automatic [W-1:0] f(input [W-1:0] in);
    parameter MW = W - 2;
    localparam [MW-1:0] MASK = '1;
    return in | MASK;
  endfunction
  initial begin
    #1;
    if (N !== 4'hf || P !== 4'hf || M !== {W{1'b1}}) o = 1;
    else if (f(8'h80) !== 8'hbf && W == 8) o = 2;
    else if (f(4'h8) !== 4'hb && W == 4) o = 3;
    else o = 16'h55;
  end
endmodule

module svfilllp1;
  bit [15:0] o4, o8;
  svfilllp1_sub #(4) s4(o4);
  svfilllp1_sub #(8) s8(o8);
  initial begin
    #2;
    if (o4 !== 16'h55 || o8 !== 16'h55) $error("FAIL %h %h", o4, o8);
    else $display("PASSED");
  end
endmodule
