module constprop1;
   class C;
      const static int gs = 1;
      static const int sg = 2;
      const int g = 3;
      const int inst_c;

      function new();
         inst_c = 10;       // OK: instance constant assigned in constructor
         this.inst_c = 11;  // OK: via this in constructor
         gs = 20;           // Error: const static
         sg = 21;           // Error: static const
         g = 22;            // Error: global const with initializer
      endfunction

      static function void bad_static();
         gs = 30;           // Error
         sg = 31;           // Error
      endfunction

      function void bad_meth();
         inst_c = 40;       // Error: outside constructor
      endfunction
   endclass

   C obj;
   initial begin
      obj = new;
      obj.gs = 50;          // Error
      obj.sg = 51;          // Error
      obj.g = 52;           // Error
      obj.inst_c = 53;      // Error
   end
endmodule
