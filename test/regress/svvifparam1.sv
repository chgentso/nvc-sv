// Virtual interfaces of interfaces with parameters
// Expected values: IEEE 1800-2017 25.9 (virtual interfaces of parameterized interfaces, one
// specialization per parameter set), 6.20.2 (parameters), 11.4.12 (concatenation), 20.6.2 ($bits).
`timescale 1ns/1ns
interface svvifparam1_if #(parameter W = 8, parameter D = 2*W) (input logic clk);
  logic [W-1:0] d;
  logic [D-1:0] dd;
  int cnt;
  function void inc(int n); cnt += n; d = d + 1; endfunction
  task automatic setd(logic [W-1:0] x); d = x; endtask
endinterface
class svvifparam1_drv #(int WW = 8);
  virtual svvifparam1_if #(.W(WW)) v;
  function new(virtual svvifparam1_if #(.W(WW)) vi); v = vi; endfunction
  task run(int x); v.setd(x); v.inc(2); v.dd = {v.d, v.d}; endtask
endclass
module svvifparam1;
  int errors = 0;
  logic clk;
  svvifparam1_if i8(clk);
  svvifparam1_if #(16) i16(clk);
  svvifparam1_if #(.W(4)) i4(clk);
  virtual svvifparam1_if v8; virtual svvifparam1_if #(16) v16; virtual svvifparam1_if #(.W(4)) v4;
  initial begin
    v8 = i8; v16 = i16; v4 = i4;
    v8.d = 8'hAB; v16.d = 16'hCDEF; v4.d = 4'h7;
    v8.dd = {v8.d, v8.d}; v16.dd = {v16.d, v16.d};
    #1;
    if (i8.d !== 8'hAB || i16.d !== 16'hCDEF || i4.d !== 4'h7) begin $error("d: %h %h %h", i8.d, i16.d, i4.d); errors++; end
    if ($bits(v8.dd) != 16 || $bits(v16.dd) != 32) begin $error("bits %0d %0d", $bits(v8.dd), $bits(v16.dd)); errors++; end
    if ($bits(i4.dd) != 8) begin $error("bits i4.dd %0d", $bits(i4.dd)); errors++; end
    if (i8.dd !== 16'hABAB || i16.dd !== 32'hCDEFCDEF) begin $error("dd: %h %h", i8.dd, i16.dd); errors++; end
    v8.inc(3); v16.inc(5);
    if (i8.cnt !== 3 || i16.cnt !== 5 || i8.d !== 8'hAC || i16.d !== 16'hCDF0) begin
      $error("inc: %0d %0d %h %h", i8.cnt, i16.cnt, i8.d, i16.d); errors++;
    end
    begin
      svvifparam1_drv #(8) d8;
      svvifparam1_drv #(16) d16;
      d8 = new(i8); d16 = new(i16);
      d8.run(8'h11); d16.run(16'h2233);
      if (i8.d !== 8'h12 || i16.d !== 16'h2234 || i8.dd !== 16'h1212 || i16.dd !== 32'h22342234 ||
          i8.cnt !== 5 || i16.cnt !== 7) begin
        $error("drv: %h %h %h %h %0d %0d", i8.d, i16.d, i8.dd, i16.dd, i8.cnt, i16.cnt); errors++;
      end
    end
    if (errors == 0) $display("PASSED");
  end
endmodule
