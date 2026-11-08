// Parameterised classes
class pair #(type K = int, type V = string);
   K key;
   V value;
   function new(K k, V v);
      key = k;
      value = v;
   endfunction
   function V get();
      return value;
   endfunction
endclass

class sized #(parameter N = 4, type T = int);
   T items[$];
   logic [N-1:0] mask;
   static int created = 0;
   function new();
      mask = ~0;
      created++;
   endfunction
   function int width();
      return N;
   endfunction
   function void add(T x);
      items.push_back(x);
   endfunction
endclass

// Member lookup in the body needs the actual type
class holder #(type C);
   C obj;
   function new(C c);
      obj = c;
   endfunction
   function int read();
      return obj.x;
   endfunction
endclass

class payload;
   int x = 77;
endclass

// Linked to itself through its own name and with an explicit specialisation
class node #(type T = int);
   T value;
   node next;
   node #(T) other;
   function new(T v);
      value = v;
   endfunction
endclass

virtual class shape #(type T = int);
   pure virtual function T area();
   function T twice();
      return area() * 2;
   endfunction
endclass

class square #(type T = int) extends shape #(T);
   T side;
   function new(T s);
      side = s;
   endfunction
   function T area();
      return side * side;
   endfunction
endclass

class plain_square extends square #(int);
   function new();
      super.new(3);
   endfunction
endclass

typedef pair #(int, int) int_pair_t;

function automatic int use_pair(pair #(int, string) p);
   return p.key;
endfunction

module svclass2;
   pair #(int, string) a;
   pair #(.V(int), .K(string)) b;
   pair c;
   int_pair_t d;
   sized #(8) s8;
   sized #(16, string) s16;
   holder #(payload) h;
   node #(int) n;
   shape #(int) sh;
   square #(int) sq;

   function automatic pair #(int, string) make(int k);
      pair #(int, string) r = new(k, "made");
      return r;
   endfunction

   initial begin
      automatic payload pl = new;
      automatic plain_square ps = new;

      a = new(1, "one");
      if (a.key != 1 || a.get() != "one") $display("FAILED positional");

      b = new("two", 2);
      if (b.key != "two" || b.get() != 2) $display("FAILED named");

      c = new(3, "three");
      if (c.get() != "three") $display("FAILED default");

      d = new(4, 5);
      if (d.get() != 5) $display("FAILED typedef");

      c = make(9);
      if (c.key != 9) $display("FAILED return");
      if (use_pair(a) != 1) $display("FAILED argument");

      s8 = new;
      s16 = new;
      if (s8.width() != 8 || s16.width() != 16) $display("FAILED value param");
      if (s8.mask !== 8'hff) $display("FAILED value in type %b", s8.mask);
      if (s16.mask !== 16'hffff) $display("FAILED value in type 2");
      s8.add(5);
      s16.add("x");
      if (s8.items[0] != 5 || s16.items[0] != "x") $display("FAILED queue");

      h = new(pl);
      if (h.read() != 77) $display("FAILED member lookup");

      n = new(1);
      n.next = new(2);
      n.other = n.next;
      if (n.next.value != 2 || n.other.value != 2) $display("FAILED self");

      sq = new(4);
      sh = sq;
      if (sh.area() != 16 || sh.twice() != 32) $display("FAILED virtual");
      sh = ps;
      if (sh.area() != 9) $display("FAILED derived from specialisation");

      $display("PASSED");
   end
endmodule
