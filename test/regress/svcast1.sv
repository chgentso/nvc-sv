// $cast of class handles, enumerations and values
class A;
   int id = 1;
   virtual function int who(); return 1; endfunction
endclass

class B extends A;
   int extra = 2;
   virtual function int who(); return 2; endfunction
endclass

class C extends B;
   virtual function int who(); return 3; endfunction
endclass

class E extends A;
   virtual function int who(); return 4; endfunction
endclass

typedef enum { RED, GREEN = 4, BLUE } color_t;

module svcast1;
   `define CHECK(x, y) if ((x) !== (y)) begin \
      $display("FAILED: %0d !== %0d", (x), (y)); $finish; end

   A a;
   B b;
   C c;
   E e;
   color_t col;
   int i, ok;
   real r;
   byte by;

   function automatic bit try_b(A h);
      B local_b;
      return $cast(local_b, h);
   endfunction

   initial begin
      c = new;
      a = c;

      // Downcast to the actual class and to a base class of it
      `CHECK($cast(c, a), 1);
      `CHECK($cast(b, a), 1);
      `CHECK(b.extra, 2);
      `CHECK(b.who(), 3);
      b = null;
      $cast(b, a);
      `CHECK(b != null, 1);

      // Not an instance of the class
      e = null;
      `CHECK($cast(e, a), 0);
      `CHECK(e == null, 1);

      a = new;
      `CHECK($cast(b, a), 0);
      `CHECK($cast(c, a), 0);
      `CHECK($cast(a, c), 1);   // Upcast always works
      `CHECK(a.who(), 3);

      // The result is a function of the dynamic type
      b = new;
      a = b;
      `CHECK(try_b(a), 1);
      e = new;
      a = e;
      `CHECK(try_b(a), 0);
      a = null;
      `CHECK(try_b(a), 1);
      `CHECK(b == null, 0);

      // Enumerations and other values
      `CHECK($cast(col, 4), 1);
      `CHECK(col, GREEN);
      `CHECK($cast(col, 5), 1);
      `CHECK(col, BLUE);
      `CHECK($cast(col, 3), 0);
      `CHECK(col, BLUE);
      i = 0;
      `CHECK($cast(col, i), 1);
      `CHECK(col, RED);
      i = 7;
      `CHECK($cast(col, i), 0);

      `CHECK($cast(i, 300), 1);
      `CHECK(i, 300);
      `CHECK($cast(by, 300), 1);
      `CHECK(by, 44);
      r = 2.5;
      `CHECK($cast(i, r), 1);
      `CHECK(i, 3);
      $cast(i, 12);
      `CHECK(i, 12);
      $display("PASSED");
   end
endmodule
