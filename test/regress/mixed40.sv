// Several continuous assignments expanded from one macro drive different
// members of a variable with a VHDL record type
`define WIRE(dst) \
assign dst.ack = 1'b1; \
assign dst.err = 1'b0; \
assign dst.stall = 1'b1;

module mixed40;
   import mixed40_pkg::*;
   t_rec r;

   `WIRE(r)

   initial begin
      #1;
      if (r.ack !== 1'b1 || r.err !== 1'b0 || r.stall !== 1'b1)
        $display("FAILED %b %b %b", r.ack, r.err, r.stall);
      else
        $display("PASSED");
      $finish;
   end
endmodule
