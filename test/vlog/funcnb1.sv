module funcnb1;
   reg x;

   function void f_sv(input logic a);
      x <= a;            // OK in SystemVerilog (IEEE 1800 13.4)
   endfunction

   final begin
      x <= 1'b0;         // Error: non-blocking assignment in final
   end
endmodule
