// Continuous assignments and port connections using members of an unpacked
// structure, which is held in memory rather than in signals
// Expected values: IEEE 1800-2017 10.3 (continuous assignments), 10.3.3 (delay and
// inertial cancelling), 23.3.3 (port connections are continuous assignments), 11.4.8
// (bitwise & with x), 11.4.3 (arithmetic with x gives x), 7.2.2 (packed member select).
// A net driven only by a delayed assignment is not sampled before its first update
// matures (implementation-defined initial value).

module svstructconn1_sub(input logic [7:0] i, output logic [7:0] o);
   assign o = i + 1;
endmodule

module svstructconn1_inv(input wire [7:0] i, output wire [7:0] o);
   assign o = ~i;
endmodule

module svstructconn1;
   typedef struct {
      real r;
      logic [7:0] x;
      struct packed { logic [3:0] a; logic [3:0] b; } p;
   } d_t;

   typedef struct { logic [7:0] a; logic [7:0] b; bit [7:0] c; } e_t;

   d_t s;
   e_t t;
   wire [7:0] w1, w2, w3, w4, w5, w7;
   wire [3:0] w6;
   logic [7:0] sig = 8'h0f;
   reg [7:0] r = 8'h10;

   assign w1 = s.x;
   assign w2 = s.x & sig;
   assign w3 = s.x + 1;
   assign w4 = {s.x[3:0], sig[3:0]};
   assign #2 w5 = s.x;
   assign w6 = s.p.b;

   svstructconn1_sub u1 (.i(t.a), .o(t.b));
   svstructconn1_inv u2 (.i(r), .o(t.c));
   svstructconn1_sub u3 (.i(t.c + 8'd1), .o(w7));

   int errs;
   task automatic ck8(string tag, logic [7:0] got, logic [7:0] exp);
      if (got !== exp) begin $error("%s: got %b expected %b", tag, got, exp); errs++; end
   endtask
   initial begin
      #1;
      ck8("w1 t1", w1, 8'bxxxxxxxx);
      ck8("w2 t1", w2, 8'b0000xxxx);
      ck8("w3 t1", w3, 8'bxxxxxxxx);
      ck8("w4 t1", w4, 8'bxxxx1111);
      if (w6 !== 4'bxxxx) begin $error("w6 t1 %b", w6); errs++; end
      ck8("t.a t1", t.a, 8'bxxxxxxxx);
      ck8("t.b t1", t.b, 8'bxxxxxxxx);
      ck8("t.c t1", t.c, 8'hef);
      ck8("w7 t1", w7, 8'hf1);
      s.x = 8'h35;
      s.p = 8'h9c;
      t.a = 8'h41;
      r = 8'h01;
      #1;
      ck8("w1 t2", w1, 8'h35);
      ck8("w2 t2", w2, 8'h05);
      ck8("w3 t2", w3, 8'h36);
      ck8("w4 t2", w4, 8'h5f);
      if (w6 !== 4'hc) begin $error("w6 t2 %h", w6); errs++; end
      ck8("t.a t2", t.a, 8'h41);
      ck8("t.b t2", t.b, 8'h42);
      ck8("t.c t2", t.c, 8'hfe);
      ck8("w7 t2", w7, 8'h00);
      #2;  // t=4: the delayed update scheduled at t=1 matured at t=3
      ck8("w5 t4", w5, 8'h35);
      sig = 8'hf0;
      t.a = 8'hfe;
      #1;
      ck8("w1 t5", w1, 8'h35);
      ck8("w2 t5", w2, 8'h30);
      ck8("w4 t5", w4, 8'h50);
      ck8("t.a t5", t.a, 8'hfe);
      ck8("t.b t5", t.b, 8'hff);
      s.x = 8'h01;
      s.x = 8'h02;
      #1;
      ck8("w1 t6", w1, 8'h02);
      #2;  // the update to 8'h01 is superseded; 8'h02 matures at t=7
      ck8("w5 t8", w5, 8'h02);
      if (errs == 0) $display("PASSED");
      $finish;
   end
endmodule
