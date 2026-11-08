// Scope resolution with packages and classes
package pkg;
   typedef enum { RED, GREEN = 5, BLUE } color_t;
   typedef struct packed { logic [3:0] hi, lo; } byte_t;
   parameter int WIDTH = 6;
   int counter = 10;
   int tab[3] = '{ 1, 2, 3 };

   function automatic int add(int a, int b);
      return a + b;
   endfunction

   task automatic bump(int by);
      counter += by;
   endtask

   class Config;
      static int created = 100;
      static function int next_id();
         return created++;
      endfunction
      static function int twice(int x);
         return x * 2;
      endfunction
      typedef int word_t;
      int inst;
   endclass
endpackage

class Util;
   static int hits;
   static function int square(int x);
      hits++;
      return x * x;
   endfunction
   static task static_task(int by);
      hits += by;
   endtask
endclass

module svscope1;
   `define CHECK(x, y) if ((x) !== (y)) begin \
      $display("FAILED: %0d !== %0d", (x), (y)); $finish; end

   pkg::color_t c;
   pkg::byte_t b;
   int v;

   function automatic pkg::color_t pick(pkg::color_t in);
      return in;
   endfunction

   initial begin
      pkg::Config::word_t w;
      pkg::color_t local_c;

      `CHECK(pkg::add(3, 4), 7);
      `CHECK(pkg::WIDTH, 6);
      `CHECK(pkg::counter, 10);
      pkg::bump(5);
      `CHECK(pkg::counter, 15);
      pkg::counter = 20;
      pkg::counter++;
      `CHECK(pkg::counter, 21);
      `CHECK(pkg::tab[1], 2);
      pkg::tab[1] = 7;
      `CHECK(pkg::tab[1], 7);

      c = pkg::GREEN;
      `CHECK(c, 5);
      `CHECK(pkg::BLUE, 6);
      local_c = pick(pkg::BLUE);
      `CHECK(local_c, pkg::BLUE);
      b = 8'hA5;
      `CHECK(b.hi, 4'hA);

      w = 3;
      `CHECK(w, 3);

      `CHECK(Util::square(5), 25);
      void'(Util::square(2));
      Util::static_task(10);
      `CHECK(Util::hits, 12);
      Util::hits = 1;
      Util::hits++;
      `CHECK(Util::hits, 2);

      `CHECK(pkg::Config::twice(4), 8);
      `CHECK(pkg::Config::next_id(), 100);
      `CHECK(pkg::Config::next_id(), 101);
      `CHECK(pkg::Config::created, 102);
      v = pkg::add(pkg::Config::twice(1), pkg::WIDTH);
      `CHECK(v, 8);
      $display("PASSED");
   end
endmodule
