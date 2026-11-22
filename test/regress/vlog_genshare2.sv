// Tree of OR gates: local parameters computed from two genvars, nested
// if-generate, and loops with unusual bounds and steps
module vlog_genshare2;
   localparam int NL = 3;
   logic [2**(NL+1)-2:0] tr;
   logic [2**NL-1:0] oh;
   logic [15:0] d, s;
   logic [7:0] n;

   for (genvar level = 0; level < NL+1; level++) begin : gen_tree
      localparam int Base0 = (2**level)-1;
      localparam int Base1 = (2**(level+1))-1;
      for (genvar offset = 0; offset < 2**level; offset++) begin : gen_level
         localparam int Pa = Base0 + offset;
         localparam int C0 = Base1 + 2*offset;
         localparam int C1 = Base1 + 2*offset + 1;
         if (level == NL) begin : gen_leafs
            assign tr[Pa] = oh[offset];
         end
         else begin : gen_nodes
            assign tr[Pa] = tr[C0] || tr[C1];
         end
      end
   end

   for (genvar i = 15; i >= 0; i--) begin : g_down
      assign d[i] = s[15 - i];
   end

   for (genvar i = 0; i < 16; i += 2) begin : g_step
      assign n[i / 2] = s[i] ^ s[i + 1];
   end

   logic [3:0] neg;
   for (genvar i = -2; i < 2; i++) begin : g_neg
      assign neg[i + 2] = (i < 0);
   end

   initial begin
      oh = '0;
      #1 if (tr !== '0) $display("FAILED zero %b", tr);
      oh = 8'h10;
      #1 if (tr !== 15'b000100000100101) $display("FAILED tree %b", tr);
      if (gen_tree[2].gen_level[1].Pa != 4 || gen_tree[3].gen_level[5].C1 != 26
          || gen_tree[1].Base1 != 3 || gen_tree[3].gen_level[6].Pa != 13)
         $display("FAILED params");
      s = 16'hb4e1;
      #1 if (d !== 16'h872d) $display("FAILED down %h", d);
      if (n !== 8'ha5) $display("FAILED step %h", n);
      if (neg !== 4'b0011) $display("FAILED neg %b", neg);
      $display("PASSED");
   end
endmodule
