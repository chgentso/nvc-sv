module castcls1;
   class base;
   endclass
   class derived extends base;
   endclass
   class unrelated;
   endclass

   base b;
   derived d;
   unrelated u;
   int n;

   initial begin
      d = new;
      b = base'(d);          // OK: upcast
      b = base'(null);       // OK: null
      d = derived'(b);       // Error: downcast requires $cast
      u = unrelated'(d);     // Error: unrelated classes
      d = derived'(n);       // Error: non-class to class
      n = int'(d);           // Error: class to non-class
   end
endmodule
