// Nonblocking assignments to virtual interface members from tasks, methods and functions.
// Expected values: IEEE 1800-2017 4.7/10.4.2 (NBA update in the NBA region), 11.5.1 (part selects,
// NBA of a part select keeps the other bits), 11.8 (truncation to the target width),
// 9.4.2 (posedge wait), 25.9 (virtual interfaces).
`timescale 1ns/1ns
interface IPort(input logic clk);
   logic alloc;
   logic [3:0] cnt;
   logic [7:0] data;
endinterface

typedef virtual IPort VIPort;

class Drv;
   VIPort vif;
   function new(VIPort v); vif = v; endfunction
   task go(int n);
      vif.cnt <= n;
      vif.data[3:0] <= n + 1;
      @(posedge vif.clk);
      vif.alloc <= 0;
   endtask
endclass

module svvifnba1;
   logic clk = 0;
   int errors = 0;
   always #5 clk = ~clk;
   IPort p(clk);
   Drv d;

   task automatic drive(VIPort port, input int n);
      port.alloc <= 1;
      port.cnt <= n;
      port.data[7:4] <= 4'ha;
      {port.data[3], port.data[2]} <= 2'b11;
      @(posedge port.clk);
      port.alloc <= 0;
   endtask

   function automatic void setf(VIPort port);
      port.cnt <= 4'd9;
   endfunction

   initial begin
      automatic VIPort v = p;
      // t=0: drive; posedge at 5 clears alloc; check at t=6
      drive(v, 5);
      #1;
      if (p.alloc !== 1'b0) begin $error("alloc=%b exp 0", p.alloc); errors++; end
      if (p.cnt !== 4'd5) begin $error("cnt=%0d exp 5", p.cnt); errors++; end
      // data[7:4]=a, data[3:2]=11, data[1:0] never written (x)
      if (p.data !== 8'b1010_11xx) begin $error("data=%b exp 101011xx", p.data); errors++; end
      d = new(p);
      d.go(7);   // t=6: cnt<=7, data[3:0]<=8; posedge at 15; alloc<=0
      #1;
      if (p.alloc !== 1'b0) begin $error("alloc=%b exp 0 (2)", p.alloc); errors++; end
      if (p.cnt !== 4'd7) begin $error("cnt=%0d exp 7", p.cnt); errors++; end
      if (p.data !== 8'ha8) begin $error("data=%h exp a8", p.data); errors++; end
      setf(v);
      #1;
      if (p.cnt !== 4'd9) begin $error("cnt=%0d exp 9", p.cnt); errors++; end
      if (errors == 0) $display("PASSED");
      $finish;
   end
endmodule
