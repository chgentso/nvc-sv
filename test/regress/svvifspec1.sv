// Specializations of virtual interfaces of parameterized interfaces (value and type parameters)
// used as parameters of a class.
// Expected values: IEEE 1800-2017 25.9, 8.25 (parameterized classes, static members),
// 6.20.3 (type parameters), 20.6.2 ($bits).
`timescale 1ns/1ns
interface pif #(parameter int W = 8, parameter type T = logic [W-1:0]) (input logic clk);
  T data;
  function automatic int width(); return $bits(T); endfunction
endinterface
class db #(type T = int);
  static T cur;
  static function void set(string k, T v); cur = v; endfunction
  static function T get(string k); return cur; endfunction
endclass
int errors = 0;
class drv #(int W = 8, type T = logic [W-1:0]);
  virtual pif #(W, T) vif;
  function void show();
    if (W != 16 || vif.width() != 16) begin $error("W=%0d width=%0d", W, vif.width()); errors++; end
  endfunction
endclass
module svvifspec1;
  logic clk;
  pif #(16) i16(clk);
  pif #(8) i8(clk);
  pif #(16, bit [15:0]) i16b(clk);
  virtual pif#(8) x;
  virtual pif#(16) y;
  virtual pif#(8, logic [7:0]) x2;
  virtual pif#(.W(16), .T(logic [15:0])) y2;
  virtual pif#(16, bit [15:0]) z;
  drv #(16) d16;
  drv #(16, bit [15:0]) d16b;
  initial begin
    d16 = new; d16b = new;
    db#(virtual pif#(8))::set("a", i8);
    db#(virtual pif#(16))::set("b", i16);
    db#(virtual pif#(16, bit [15:0]))::set("c", i16b);
    x = db#(virtual pif#(8))::get("a");
    y = db#(virtual pif#(16))::get("b");
    z = db#(virtual pif#(16, bit [15:0]))::get("c");
    x.data = 8'h12; y.data = 16'h3456; z.data = 16'hf00d;
    if (i8.data !== 8'h12 || i16.data !== 16'h3456 || i16b.data !== 16'hf00d) begin
      $error("1: %h %h %h", i8.data, i16.data, i16b.data); errors++;
    end
    x2 = db#(virtual pif#(8, logic [7:0]))::get("a");
    y2 = db#(virtual pif#(.W(16), .T(logic [15:0])))::get("b");
    x2.data = 8'h99; y2.data = 16'h1234;
    if (i8.data !== 8'h99 || i16.data !== 16'h1234) begin $error("2: %h %h", i8.data, i16.data); errors++; end
    d16.vif = i16; d16b.vif = i16b;
    d16.show(); d16b.show();
    if (errors == 0) $display("PASSED");
  end
endmodule
