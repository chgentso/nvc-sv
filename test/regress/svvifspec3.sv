interface pif #(parameter int W = 8) (input logic clk);
  logic [W-1:0] data;
endinterface
class db #(type T = int);
  static T cur;
  static function void set(T v); cur = v; endfunction
  static function T get(); return cur; endfunction
endclass
module svvifspec3;
  logic clk;
  pif #(16) i16(clk);
  pif #(8) i8(clk);
  virtual pif#(8) x;
  virtual pif#(16) y;
  initial begin
    db#(virtual pif#(8))::set(i8);
    db#(virtual pif#(16))::set(i16);
    x = db#(virtual pif#(8))::get();
    y = db#(virtual pif#(16))::get();
    x.data = 8'h12; y.data = 16'h3456;
    $display("%h %h", i8.data, i16.data);
  end
endmodule
