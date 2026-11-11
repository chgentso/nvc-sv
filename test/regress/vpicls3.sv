// Validation of object addresses and pinning.  The simulation drops
// references to two objects and then allocates until there have been
// collections: the heap is small (see testlist.txt).  The C side is
// test/vpi/vpicls3.c.

class node;
   int  v;
   node next;

   function new(int x);
      v = x;
   endfunction
endclass

module vpicls3;
   node keep, dead, pinme;
   int  big[];

   initial begin
      keep = new(1);
      dead = new(2);
      pinme = new(3);
      pinme.next = new(4);   // Reachable only through pinme
      #2;
      dead = null;
      pinme = null;
      for (int i = 0; i < 60; i++) begin
         big = new[50000];
         big[i] = i;
      end
      #10;
      $display("%0d", keep.v);
   end
endmodule
