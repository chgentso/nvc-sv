// Expected values: IEEE 1800-2017 25.9 (virtual interfaces: reads and writes go to the referenced instance),
// 10.4.2 (nonblocking assignments update in the NBA region, visible after #1), 13.4 / 8.10 (task and method calls)
interface IPort(input logic clk);
   logic arr[2];
   logic [3:0] w[2];
   logic [7:0] m[2][3];
endinterface
typedef virtual IPort VIPort;
class Drv;
   VIPort vif;
   function new(VIPort v); vif = v; endfunction
   task go(int i);
      vif.w[i] = 4'h9;
      vif.w[i][3:2] = 2'b01;
      vif.m[1][i] <= 8'h5a;
   endtask
endclass
module svvifarr1;
   logic clk = 0;
   IPort p(clk);
   Drv d;
   int k = 1;
   int errs = 0;
   task automatic setk(VIPort v, int i, logic [3:0] x);
      v.w[i] <= x;
   endtask
   initial begin
      automatic VIPort v = p;
      v.arr[1] = 1;
      v.w[0] = 5;
      v.w[1][1] = 1;
      p.arr[0] = 1;
      #1;
      if (p.arr[0] !== 1'b1 || p.arr[1] !== 1'b1 || v.arr[1] !== 1'b1 || p.w[0] !== 4'h5 || p.w[1] !== 4'bxx1x)
         begin errs++; $error("step1: %b %b %b %b %b", p.arr[0], p.arr[1], v.arr[1], p.w[0], p.w[1]); end
      v.arr[0] <= 0;
      v.w[k] <= 4'hc;
      #1;
      if (p.arr[0] !== 1'b0 || p.arr[1] !== 1'b1 || p.w[1] !== 4'hc || v.w[k] !== 4'hc)
         begin errs++; $error("step2: %b %b %h %h", p.arr[0], p.arr[1], p.w[1], v.w[k]); end
      d = new(p);
      d.go(2 - 1);
      #1;
      if (p.w[1] !== 4'h5 || p.m[1][1] !== 8'h5a)
         begin errs++; $error("step3: %h %h", p.w[1], p.m[1][1]); end
      setk(v, 0, 4'h7);
      #1;
      if (p.w[0] !== 4'h7 || v.w[0] !== 4'h7)
         begin errs++; $error("step4: %h %h", p.w[0], v.w[0]); end
      if (errs == 0) $display("PASSED");
   end
endmodule
