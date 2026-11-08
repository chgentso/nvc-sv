// Expected values: IEEE 1800-2017 25.9 (virtual interface returned by a function or method; module-level virtual
// interface initialised by an instance; writes through it update the instance), 13.4, 6.8
// A virtual interface as the result of a function and initialised by an instance

interface svvifinit1_if #(parameter W = 8) (input logic clk);
  logic [W-1:0] d;
endinterface

class svvifinit1_holder;
  virtual svvifinit1_if #(8) v;
  function virtual svvifinit1_if #(8) get(); return v; endfunction
endclass

module svvifinit1;
  logic clk;
  svvifinit1_if i8(clk);
  svvifinit1_if #(16) i16(clk);
  svvifinit1_holder h = new;
  virtual svvifinit1_if #(16) init = i16;
  function virtual svvifinit1_if #(8) pick(); return i8; endfunction
  virtual svvifinit1_if #(8) w;
  int errs = 0;
  initial begin
    h.v = i8;
    w = h.get();
    w.d = 8'h5a;
    #1; if (i8.d !== 8'h5a) begin errs++; $error("i8.d=%h, expected 5a", i8.d); end
    w = pick(); w.d = 8'h33;
    #1; if (i8.d !== 8'h33) begin errs++; $error("i8.d=%h, expected 33", i8.d); end
    init.d = 16'habcd;
    #1; if (i16.d !== 16'habcd) begin errs++; $error("i16.d=%h, expected abcd", i16.d); end
    if (errs == 0) $display("PASSED");
  end
endmodule
