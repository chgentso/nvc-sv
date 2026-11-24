package misstatic1;
   class A;
      int m = init_m();
      function int init_m(); return 1; endfunction
      function void ns(); endfunction
      function int nsf(); return 2; endfunction
      static function void st(); endfunction
   endclass

   class B;
      function void f();
         A::ns();               // Error: non-static called via scope from unrelated class
         void'(A::nsf());       // Error: non-static function called via scope
         A::st();               // OK: static
      endfunction
   endclass

   class D extends A;
      function void ok();
         ns();                  // OK: non-static method of derived class
         A::ns();               // OK: superclass method from derived non-static method
         st();                  // OK: static
      endfunction
      static function void bad();
         int x = nsf();         // Error: non-static function from static method
         ns();                  // Error: non-static from static method
         st();                  // OK: static
      endfunction
   endclass
endpackage

module misstatic1_m;
   import misstatic1::*;
   initial begin
      A::ns();                  // Error: non-static from module
      A::st();                  // OK: static
   end
endmodule
