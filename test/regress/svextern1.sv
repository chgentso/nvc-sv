// Methods declared extern in a class and defined outside of it
package extpkg;
   class Counter;
      int count;
      local int step = 2;
      extern function new(int start = 0);
      function int next();
         count += step;
         return count;
      endfunction
   endclass

   function Counter::new(int start = 0);
      count = start;
   endfunction
endpackage

class Base;
   int x;
   static int created;
   function new();
      created++;
   endfunction
   extern virtual function int get();
   extern function int twice(int a, int b = 3);
   extern task wait_and_set(int v);
   extern static function int how_many();
   extern function int fact(int n);
endclass

class Derived extends Base;
   int y;
   extern virtual function int get();
   extern function void set_both(int a, int b);
endclass

function int Base::get();
   return x;
endfunction

function int Base::twice(int a, int b = 3);
   int sum;
   sum = a + b + x;
   return sum * 2;
endfunction

task Base::wait_and_set(int v);
   #5;
   x = v;
endtask

function int Base::how_many();
   return created;
endfunction

function int Base::fact(int n);
   if (n <= 1)
      return 1;
   return n * fact(n - 1);
endfunction

function int Derived::get();
   return super.get() + y;
endfunction

function void Derived::set_both(int a, int b);
   x = a;
   y = b;
endfunction

module svextern1;
   `define CHECK(x, y) if ((x) !== (y)) begin \
      $display("FAILED: %0d !== %0d", (x), (y)); $finish; end

   Base b;
   Derived d;
   extpkg::Counter c;

   initial begin
      b = new;
      b.x = 4;
      `CHECK(b.get(), 4);
      `CHECK(b.twice(1), (1 + 3 + 4) * 2);
      `CHECK(b.twice(1, 2), (1 + 2 + 4) * 2);
      `CHECK(b.fact(5), 120);
      b.wait_and_set(9);
      `CHECK($time, 5);
      `CHECK(b.x, 9);

      d = new;
      d.set_both(3, 10);
      `CHECK(d.get(), 13);
      b = d;
      `CHECK(b.get(), 13);   // Virtual dispatch to the external definition
      `CHECK(Base::how_many(), 2);
      c = new(10);
      `CHECK(c.count, 10);
      `CHECK(c.next(), 12);
      c = new;
      `CHECK(c.count, 0);
      $display("PASSED");
   end
endmodule
