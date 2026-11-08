// Interface classes and implements
interface class IPut;
   pure virtual function void put(int v);
endclass

interface class IGet;
   pure virtual function int get();
endclass

interface class IBoth extends IPut, IGet;
   pure virtual function int count();
endclass

class Fifo implements IBoth;
   protected int q[$];
   virtual function void put(int v);
      q.push_back(v);
   endfunction
   virtual function int get();
      return q.pop_front();
   endfunction
   virtual function int count();
      return q.size();
   endfunction
endclass

// Methods inherited from the base class implement the interface
class Base;
   int hits;
   virtual function int get();
      hits++;
      return 100;
   endfunction
endclass

class Derived extends Base implements IGet;
endclass

class Doubler extends Fifo;
   virtual function void put(int v);
      super.put(v * 2);
   endfunction
endclass

virtual class Partial implements IGet;
   pure virtual function int get();
   function int twice();
      return get() * 2;
   endfunction
endclass

class Concrete extends Partial;
   virtual function int get();
      return 21;
   endfunction
endclass

module svifc1;
   `define CHECK(x, y) if ((x) !== (y)) begin \
      $display("FAILED: %0d !== %0d", (x), (y)); $finish; end

   IPut p;
   IGet g;
   IBoth both;
   Fifo f;
   Doubler d;
   Derived dv;
   Concrete cc;
   Partial pt;

   function automatic void fill(IPut target, int n);
      for (int i = 1; i <= n; i++)
         target.put(i);
   endfunction

   function automatic int drain(IGet source, int n);
      int sum = 0;
      for (int i = 0; i < n; i++)
         sum += source.get();
      return sum;
   endfunction

   initial begin
      f = new;
      p = f;
      g = f;
      both = f;
      fill(p, 4);
      `CHECK(both.count(), 4);
      `CHECK(drain(g, 4), 10);
      `CHECK(both.count(), 0);

      d = new;
      fill(d, 3);
      `CHECK(drain(d, 3), 12);   // Virtual calls reach the override
      p = d;
      p.put(10);
      g = d;
      `CHECK(g.get(), 20);

      dv = new;
      g = dv;
      `CHECK(g.get(), 100);
      `CHECK(dv.hits, 1);

      cc = new;
      pt = cc;
      g = pt;
      `CHECK(g.get(), 21);
      `CHECK(pt.twice(), 42);

      // Dynamic casts to and from an interface class
      g = f;
      `CHECK($cast(both, g), 1);
      `CHECK($cast(p, g), 1);
      g = dv;
      `CHECK($cast(both, g), 0);
      `CHECK($cast(f, g), 0);
      g = f;
      `CHECK($cast(f, g), 1);
      `CHECK($cast(g, cc), 1);
      g = null;
      `CHECK(g == null, 1);
      $display("PASSED");
   end
endmodule
