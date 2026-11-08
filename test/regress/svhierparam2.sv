// Expected values: IEEE 1800-2017 23.10.? / 25 (interface parameters used as hierarchical constants in a module
// instantiation, 23.6), 23.3.3 (port connections, widths). VHDL side (IEEE 1076): q <= (others => a(0)), so every
// bit of res equals bit 0 of addr, and the generics fix the widths (g_w = 6, g_n = 3).
`timescale 1ns/1ns
interface IPort(input logic clk);
   parameter pw = 6;
   parameter np = 3;
   logic [pw-1:0] addr;
   logic [np-1:0] res;
endinterface

module svhierparam2;
   logic clk = 0;
   IPort p(clk);
   svhierparam2_ent #(.g_w(p.pw), .g_n(p.np)) u(.a(p.addr), .q(p.res));
   int err = 0;
   initial begin
      p.addr = 6'h3f; #2;
      if (p.res !== 3'b111) begin err++; $error("res=%b expected 111", p.res); end
      p.addr = 6'h3e; #2;
      if (p.res !== 3'b000) begin err++; $error("res=%b expected 000", p.res); end
      p.addr = 6'h01; #2;
      if (p.res !== 3'b111) begin err++; $error("res=%b expected 111", p.res); end
      if ($bits(p.res) != 3 || $bits(p.addr) != 6) begin err++; $error("widths"); end
      if (err == 0) $display("PASSED");
   end
endmodule
