// A case generate construct may be written directly as an item of a
// module or interface without a generate region (IEEE 1800-2017 27.3,
// 27.5; A.1.4 module_or_generate_item and interface_or_generate_item).

interface vlogcasegen2_if #(parameter int K = 1);
   case (K)
     1: begin : one logic [3:0] v = 4'h1; end
     default: begin : other logic [3:0] v = 4'hf; end
   endcase
endinterface

module vlogcasegen2;
   localparam int SEL = 2;

   case (SEL)
     0: begin : g logic [7:0] v = 8'h00; end
     1, 2: begin : g logic [7:0] v = 8'h12; end
     default: begin : g logic [7:0] v = 8'hff; end
   endcase

   case (SEL + 1)
     3: begin logic w = 1'b1; end    // genblk2: second construct of the scope
     default: begin logic w = 1'b0; end
   endcase

   vlogcasegen2_if #(.K(1)) i1 ();
   vlogcasegen2_if #(.K(5)) i5 ();

   initial begin
      #1;
      if (g.v !== 8'h12) $fatal(1, "g.v = %h", g.v);
      if (genblk2.w !== 1'b1) $fatal(1, "genblk2.w = %b", genblk2.w);
      if (i1.one.v !== 4'h1) $fatal(1, "i1.one.v = %h", i1.one.v);
      if (i5.other.v !== 4'hf) $fatal(1, "i5.other.v = %h", i5.other.v);
      $display("PASSED");
   end
endmodule
