// A class parameterized by two virtual interface specializations keeps separate static storage.
// Expected values: IEEE 1800-2017 25.9, 8.25 (each specialization has its own static members).
interface pif #(parameter int W = 8) (input logic clk);
  logic [W-1:0] data;
endinterface
class db #(type T = int);
  static T cur;
  static function void set(T v); cur = v; endfunction
  static function T get(); return cur; endfunction
endclass
module svvifspec2;
  logic clk;
  pif #(16) i16(clk);
  pif #(8) i8(clk);
  virtual pif#(8) x;
  virtual pif#(16) y;
  int errors = 0;
  initial begin
    db#(virtual pif#(8))::set(i8);
    db#(virtual pif#(16))::set(i16);
    x = db#(virtual pif#(8))::get();
    y = db#(virtual pif#(16))::get();
    x.data = 8'h12; y.data = 16'h3456;
    if (i8.data !== 8'h12 || i16.data !== 16'h3456) begin $error("%h %h", i8.data, i16.data); errors++; end
    
    if (errors == 0) $display("PASSED");
  end
endmodule
