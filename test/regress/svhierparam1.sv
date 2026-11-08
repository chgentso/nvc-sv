// Parameters of instances referenced hierarchically (interface and module
// parameters, parameter references in generate loops) as parameter overrides.
// Every leaf checks the values it received against literal expectations that
// were computed by hand from the declarations, and the time it fires.
// Expected values: IEEE 1800-2017 23.10 (overriding parameters), 23.6
// (hierarchical references to parameters), interface parameters (25.3),
// 27.4 (generate loops).
interface IPort #(parameter int pw = 6, parameter int np = 3)(input logic clk);
   parameter int extra = pw * 2;
   logic [pw-1:0] addr;
endinterface

module sub #(parameter int W = 5)();
   localparam int DW = W + 1;
endmodule

module leaf #(parameter int pw = 1, parameter int np = 1, parameter int w = 0,
              parameter int epw = -1, parameter int enp = -1, parameter int ew = -1) ();
   initial #(pw + np) begin
      svhierparam1.n++;
      if (pw != epw || np != enp || w != ew) begin
         $error("%m: got pw=%0d np=%0d w=%0d expected pw=%0d np=%0d w=%0d",
                pw, np, w, epw, enp, ew);
         svhierparam1.errors++;
      end
      if ($time != epw + enp) begin
         $error("%m: fired at %0t expected %0d", $time, epw + enp);
         svhierparam1.errors++;
      end
   end
endmodule

module svhierparam1;
   int n = 0, errors = 0;
   logic clk = 0;
   IPort p(clk);                         // pw=6 np=3 extra=12
   IPort #(.pw(9), .np(2)) p2(clk);      // pw=9 np=2 extra=18
   sub #(.W(7)) s();                     // W=7 DW=8
   leaf #(.pw(p.pw), .np(p.np), .w(s.W), .epw(6), .enp(3), .ew(7)) u();
   leaf #(.pw(p2.pw + 1), .np(p2.np), .w(s.DW * 2), .epw(10), .enp(2), .ew(16)) u2();
   leaf #(.pw(p.extra), .np(1), .w(p2.extra), .epw(12), .enp(1), .ew(18)) u3();
   for (genvar i = 0; i < 2; i++) begin : g
      leaf #(.pw(i + p2.np), .np(p.np), .w(s.W), .epw(i + 2), .enp(3), .ew(7)) u();
   end
   initial begin
      #100;
      if (n != 5) begin $error("%0d leaves fired, expected 5", n); errors++; end
      if ($bits(p.addr) != 6 || $bits(p2.addr) != 9) begin $error("addr widths wrong"); errors++; end
      if (errors == 0) $display("PASSED");
   end
endmodule
