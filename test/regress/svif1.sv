// Interfaces, virtual interfaces, nested classes, and hierarchical calls
virtual class accessor;
   pure virtual task automatic poke(int v);
   pure virtual function int peek();
endclass

interface ifc (input logic clk);
   parameter W = 8;
   logic [W-1:0] data;
   int           pokes = 0;
   mailbox #(int) q;

   modport master (output data);

   struct {
      int mode;
      int limit;
   } settings;

   class impl extends accessor;
      task automatic poke(int v);
         pokes++;
         q.put(v);
         @(posedge clk);
      endtask

      function int peek();
         return pokes;
      endfunction
   endclass

   impl obj;

   function automatic impl get();
      return obj;
   endfunction

   initial begin
      int v;
      obj = new;
      q = new();
      settings.limit = 3;
      forever begin
         q.get(v);
         data <= v;
      end
   end
endinterface

interface mon (input logic a, input logic b);
endinterface

typedef virtual mon vmon;

class watcher;
   vmon m;
   function new(vmon m);
      this.m = m;
   endfunction
   function int both();
      return m.a == 1'b1 && m.b == 1'b1;
   endfunction
endclass

module svif1;
   logic clk = 0;
   logic a = 0, b = 1;

   always #5ns clk = ~clk;

   ifc #(.W(16)) bus (clk);
   mon watch (.a(a), .b(b));

   accessor acc;
   watcher w;

   initial begin
      #1ns;
      acc = bus.get();
      bus.settings.mode = 2;
      if (bus.settings.mode != 2 || bus.settings.limit != 3)
         $display("FAILED settings");
      acc.poke(1234);
      #1ns;
      if (bus.data !== 16'd1234) $display("FAILED data %0d", bus.data);
      if (acc.peek() != 1) $display("FAILED peek");
      w = new(watch);
      if (w.both()) $display("FAILED both 1");
      a = 1;
      #1ns;
      if (!w.both()) $display("FAILED both 2");
      $display("PASSED");
      $finish;
   end
endmodule
