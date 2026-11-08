// A semicolon after an include directive is ignored
`include "svinc/a/b/m.vh";

module svincsemi1;
  wire [7:0] o;
  svincrel_m u(.o(o));
  initial begin
    #1;
    if (o === 8'hff) $display("PASSED");
    else $display("FAILED %h", o);
  end
endmodule
