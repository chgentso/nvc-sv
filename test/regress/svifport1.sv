// Interface ports and modports
typedef struct packed { logic [3:0] hi; logic [3:0] lo; } pair_t;

interface bus_if #(parameter int W = 8) (input logic clk);
   logic [W-1:0] data;
   logic         valid;
   pair_t        pair;
   logic [3:0]   mem [0:3];
   int           count = 0;
   localparam int DOUBLE = W * 2;

   task automatic send(input logic [W-1:0] d);
      @(negedge clk);
      data <= d;
      valid <= 1'b1;
      @(negedge clk);
      valid <= 1'b0;
   endtask

   function automatic int width();
      return W;
   endfunction

   modport master (output data, output valid, input clk, import send);
   modport slave (input data, input valid, input clk, import width);
endinterface

module consumer (bus_if.slave s, input logic en, output logic [15:0] seen);
   initial seen = '0;
   always @(posedge s.clk)
     if (en && s.valid) seen <= s.data;
endmodule

module producer #(parameter int V = 1) (bus_if.master m);
   initial begin
      m.valid = 0;
      m.send(V);
      m.send(V + 1);
   end
endmodule

// Generic interface port, passed down to a child
module relay (interface b, input logic en, output logic [15:0] seen);
   consumer u_c (.s(b), .en, .seen);
endmodule

// Interface port before and after ordinary ports, by position
module mixed (input logic a, bus_if b, output logic y, bus_if c);
   assign y = a & b.valid;
   assign c.pair = 8'h5a;
endmodule

// Output of a child drives a member of the interface
module drv_leaf (output logic [3:0] o);
   assign o = 4'd9;
endmodule

module drv_mem (bus_if x);
   drv_leaf u (.o(x.mem[2]));
   for (genvar i = 0; i < 2; i++) begin : g
      assign x.mem[i] = i + 1;
   end
endmodule

class Driver;
   virtual bus_if.master vif;
   function new(virtual bus_if.master v);
      vif = v;
   endfunction
   task run(int d);
      vif.send(d);
   endtask
endclass

module with_vif (bus_if h);
   virtual bus_if vi;
   initial begin
      Driver dr;
      vi = h;
      #0;
      if (vi.width() != 8) $fatal(1, "width");
      dr = new(h);
      dr.run(77);
   end
endmodule

module svifport1;
   logic clk = 0, en = 1;
   always #5 clk = ~clk;

   bus_if #(8)  a (clk);
   bus_if #(16) w (clk);
   bus_if       c (.clk);
   bus_if       d (clk);
   bus_if       e (clk);
   bus_if       f (clk);
   logic [15:0] seen_a, seen_w, seen_e;
   logic y;

   producer #(.V(16'h11))  u_pa (a.master);
   producer #(.V(16'h1234)) u_pw (.m(w));
   consumer u_ca (a.slave, en, seen_a);
   relay    u_rw (.b(w), .en, .seen(seen_w));
   mixed    u_mx (en, a, y, c);
   drv_mem  u_dm (d);
   with_vif u_wv (.h(e));
   consumer u_ce (.s(svifport1.e), .en(en), .seen(seen_e));

   // Implicit connection by name
   bus_if h (clk);
   drv_mem2 u_dm2 (.*);

   initial begin
      #100;
      $display("seen_a=%h seen_w=%h seen_e=%h", seen_a, seen_w, seen_e);
      $display("y=%b pair=%h hi=%h", y, c.pair, c.pair.hi);
      $display("mem=%h %h %h", d.mem[0], d.mem[1], d.mem[2]);
      $display("through=%h", u_rw.b.width());
      $display("up=%h", u_mx.c.pair.lo);
      $display("h=%h", h.pair);
      if (seen_a !== 16'h0012 || seen_w !== 16'h1235 || seen_e !== 16'h004d)
        $fatal(1, "bad seen");
      $display("PASSED");
      $finish;
   end
endmodule

module drv_mem2 (bus_if h);
   assign h.pair.lo = 4'h3;
   assign h.pair.hi = h.DOUBLE;
endmodule
