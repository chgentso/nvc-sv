// Constant function calls in parameter overrides, generate conditions and localparams
// Expected values: IEEE 1800-2017 13.4.3 (constant functions), 6.20.2 (parameters),
// 27.3/27.4 (generate loops and conditionals), 23.10.2 (parameter overrides), 9.4.1 (delay)
`timescale 1ns/1ns
function automatic bit cf(int i); return i >= 1; endfunction
function automatic int twice(int i); return 2 * i; endfunction
function automatic int sum3(int i); return twice(i) + cf(i) + 1; endfunction

package pk;
   function automatic int sq(int i); return i * i; endfunction
   function automatic int sq1(int i); return sq(i) + 1; endfunction
endpackage

module leaf #(parameter bit g = 0, parameter int w = 0, parameter int p = 0) ();
   int fired = -1;
   initial #(1 + p) fired = int'($time);
endmodule

module svconstfn1;
   localparam int lp = sum3(2);
   int errs = 0;
   int lp_fired = -1;
   for (genvar i = 0; i < 3; i++) begin : gen
      leaf #(.g(cf(i)), .w(sum3(i)), .p(pk::sq1(i))) u();
      if (twice(i) > 1) begin : big
         int fired = -1;
         initial #(10 + i) fired = int'($time);
      end
   end
   initial #20 lp_fired = int'($time);
   function automatic void chk(string m, longint got, longint exp);
     if (got !== exp) begin $error("%s: got %0d expected %0d", m, got, exp); errs++; end
   endfunction
   initial begin
     #25;
     // g = (i >= 1), w = twice(i)+g+1, p = i*i+1
     chk("g0", gen[0].u.g, 0); chk("w0", gen[0].u.w, 1); chk("p0", gen[0].u.p, 1);
     chk("g1", gen[1].u.g, 1); chk("w1", gen[1].u.w, 4); chk("p1", gen[1].u.p, 2);
     chk("g2", gen[2].u.g, 1); chk("w2", gen[2].u.w, 6); chk("p2", gen[2].u.p, 5);
     // leaf i fires at 1+p
     chk("t0", gen[0].u.fired, 2); chk("t1", gen[1].u.fired, 3); chk("t2", gen[2].u.fired, 6);
     // generate-if exists for i = 1, 2 only (twice(i) > 1), fires at 10+i
     chk("big1", gen[1].big.fired, 11); chk("big2", gen[2].big.fired, 12);
     chk("lp", lp, 6); chk("lpt", lp_fired, 20);
     if (errs == 0) $display("PASSED");
   end
endmodule
