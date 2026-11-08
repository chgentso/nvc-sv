// Port redeclared with a range that calls a constant function
module sub #(parameter W = 7) (o, p);
  function integer clogb2;
    input [31:0] value;
    begin
      for (clogb2 = 0; value > 0; clogb2 = clogb2 + 1)
        value = value >> 1;
    end
  endfunction

  output [((clogb2(32'h7fffffff-32'h0)-2)+2-1):2] o;
  wire   [((clogb2(32'h7fffffff-32'h0)-2)+2-1):2] o;
  output [clogb2(W)-1:0] p;
  wire   [clogb2(W)-1:0] p;
  assign o = 29'h1fffffff;
  assign p = ~0;
endmodule

module svconstrange1;
  wire [30:2] o;
  wire [3:0]  p;
  sub #(.W(9)) u(o, p);

  initial begin
    #1;
    if (o !== 29'h1fffffff) $fatal(1, "o = %h", o);
    if (p !== 4'hf) $fatal(1, "p = %h", p);
    $display("PASSED");
  end
endmodule
