// Generic of user defined enumeration type set from an enum value
typedef enum {CLASSIC, PIPELINED, BURST} mode_sv_t;

module mixed35;
   wire [31:0] o1, o2, o3;

   mixed35_sub #(.g(PIPELINED)) u1 (.o(o1));
   mixed35_sub #(.g(BURST)) u2 (.o(o2));
   mixed35_sub u3 (.o(o3));

   initial begin
      #1;
      if (o1 !== 1 || o2 !== 2 || o3 !== 0)
        $display("FAILED %0d %0d %0d", o1, o2, o3);
      else
        $display("PASSED");
      $finish;
   end
endmodule
