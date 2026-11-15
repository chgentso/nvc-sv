// A slice of an array of interface instances can be connected to an array
// of interface ports (IEEE 1800-2017 25.5, 23.3.3).

interface ifc;
   logic [7:0] value;
endinterface

module leaf (ifc ports [1:0], output logic [7:0] sum);
   always_comb sum = ports[0].value + ports[1].value;
endmodule

module vlogiarrayslice1;
   ifc iarr [3:0] ();
   ifc up [0:3] ();
   logic [7:0] s0, s1, s2;

   leaf l0 (.ports(iarr[3:2]), .sum(s0));
   leaf l1 (.ports(iarr[1:0]), .sum(s1));
   leaf l2 (.ports(up[1:2]), .sum(s2));

   initial begin
      iarr[0].value = 1;
      iarr[1].value = 2;
      iarr[2].value = 4;
      iarr[3].value = 8;
      up[0].value = 16;
      up[1].value = 32;
      up[2].value = 64;
      up[3].value = 128;
      #1;
      if (s0 !== 8'd12) $fatal(1, "s0 %0d", s0);
      if (s1 !== 8'd3) $fatal(1, "s1 %0d", s1);
      if (s2 !== 8'd96) $fatal(1, "s2 %0d", s2);
      $display("PASSED");
   end
endmodule
