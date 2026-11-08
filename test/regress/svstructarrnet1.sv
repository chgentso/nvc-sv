// Expected values: IEEE 1800-2017 7.2.1/10.9.2 (unpacked structure array with
// assignment patterns), 7.4.6 (a read of a nonexistent array element returns the
// default value of the element type: 0 for 2-state bit), 10.3 (continuous assignments
// re-evaluate when an operand, including the index, changes), 6.10 (implicit net
// for the left-hand side identifier dv2)
typedef struct { bit mul; bit [3:0] div; } t_cfg;
module svstructarrnet1;
   t_cfg v[2] = '{ '{mul: 0, div: 4}, '{mul: 1, div: 9} };
   int k = 2;
   int errs;
   wire dv = v[k - 1].mul;
   wire dp = v[k].mul;
   wire [3:0] dd = v[k].div;
   assign dv2 = v[k].mul;
   task automatic chk(string tag, logic edv, logic edp, logic [3:0] edd, logic edv2);
      if (dv !== edv || dp !== edp || dd !== edd || dv2 !== edv2) begin
         $error("%s: got %b %b %h %b expected %b %b %h %b", tag, dv, dp, dd, dv2, edv, edp, edd, edv2);
         errs++;
      end
   endtask
   initial begin
      #1 chk("k=2", 1, 0, 4'h0, 0);
      k = 0;
      #1 chk("k=0", 0, 0, 4'h4, 0);
      k = 1;
      #1 chk("k=1", 0, 1, 4'h9, 1);
      v[1].mul = 0; v[1].div = 3;
      #1 chk("v[1] changed", 0, 0, 4'h3, 0);
      k = -1;
      #1 chk("k=-1", 0, 0, 4'h0, 0);
      if (errs == 0) $display("PASSED");
   end
endmodule
