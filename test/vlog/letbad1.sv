module letbad1;
   let add1(x) = x + 1;
   int y;
   initial begin
      y = add1(2);       // OK: expression
      void'(add1(3));    // OK: expression inside void cast
      add1(4);           // Error: let called as statement
   end
endmodule
