// Toggle coverage of the nets, variables and ports of a module: scalars
// and vectors, two-state and four-state types, packed structures, an enum
// and a packed array.  Types which are not bits (real, string, class
// handles, dynamic arrays) and the memory have no items.  A change to or
// from x or z is not counted by default.

class vlogcover12_cls;
   int x;
endclass

module vlogcover12_sub (input wire [1:0] a, input clk, output reg q,
                        output wire [1:0] w);
   assign w = ~a;
   always @(posedge clk) q <= a[0];
endmodule

module vlogcover12;
   typedef struct packed {
      logic       valid;
      logic [1:0] tag;
   } hdr_t;

   typedef enum logic [1:0] { S0, S1, S2 } state_t;

   reg        clk = 0;
   reg [3:0]  v = 0;
   reg        s = 0;
   wire       n;
   wire [1:0] w, w2;
   wire       q;
   bit [1:0]  b2 = 0;
   int        i = 0;
   byte       by = 0;
   hdr_t      hdr = 0;
   state_t    st = S0;
   logic [1:0][1:0] pk = 0;
   logic [1:0] mem [0:1];      // Not counted: a memory
   real       r;
   string     str;
   int        dyn [];
   vlogcover12_cls obj;
   reg [1:0]  a = 0;

   assign n = s;
   assign w2 = v[3:2];

   vlogcover12_sub u (.a(a), .clk(clk), .q(q), .w(w));

   always #1 clk = ~clk;

   initial begin
      #0.5;
      v = 4'b0001;
      #2 v = 4'b0011; s = 1; i = 5; b2 = 1; by = 8'hf0;
      hdr.tag = 2'b10; st = S2; pk[1][0] = 1; a = 2'b01;
      #2 v = 4'b0010; b2 = 3; hdr.valid = 1; st = S1; a = 2'b10; r = 1.5;
      str = "x";
      #2 v = 4'b1z0x;       // x and z are not counted
      #2 v = 4'b0000;       // from x and z neither
      #2 $display("PASSED");
      $finish;
   end
endmodule
