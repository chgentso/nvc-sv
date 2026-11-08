// Types from a VHDL package, record ports, and reading a VHDL signal
import mixed30_pkg::*;

module mixed30;
   reg clk = 0;
   t_in_array in;
   t_grid grid;
   t_out single;
   wire irq;

   mixed30_sub dut (.clk(clk), .i(in), .o(grid), .ifo(single), .unused());

   assign irq = dut.irq;

   always #5 clk = ~clk;

   initial begin
      in[0].valid = 0;
      in[0].data = 8'h00;
      in[1].valid = 0;
      in[1].data = 8'h00;
      #12;
      in[0].data = 8'h5a;
      in[0].valid = 1;
      in[1].data = 8'hc3;
      in[1].valid = 1;
      #10;
      if (grid[1][2].data !== 8'h5a) $display("FAILED grid %x", grid[1][2].data);
      if (grid[1][2].ready !== 1'b1) $display("FAILED ready");
      if (single.data !== 8'hc3) $display("FAILED single %x", single.data);
      if (single.ready !== 1'b1) $display("FAILED single ready");
      if (irq !== 1'b1) $display("FAILED irq %b", irq);
      $display("PASSED");
      $finish;
   end
endmodule
