`include "../defs.vh"
module svincrel_m(output [`W-1:0] o);
  assign o = {`W{1'b1}};
endmodule
