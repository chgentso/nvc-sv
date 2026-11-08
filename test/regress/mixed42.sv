// A constant driving a VHDL port is an event in the first delta cycle
module mixed42;
   wire [31:0] o;

   mixed42_sub u (.en(1'b1), .b(1'b1), .v(2'b01), .o(o));

   initial begin
      #1;
      if (o !== 111)
        $display("FAILED %0d", o);
      else
        $display("PASSED");
      $finish;
   end
endmodule
