// Dynamic arrays, queues and associative arrays, mailboxes, events and
// structures that are variables of a module or properties of an object.  The
// C side is test/vpi/vpicls2.c.

class item;
   int v;

   function new(int x);
      v = x;
   endfunction
endclass

typedef struct {
   int    a;
   string s;
} rec_t;

class box;
   int          di[];        // Dynamic array
   int          qi[$];       // Queue
   int          aai[int];    // Associative arrays: by integer, by string
   string       aas[string];
   item         qh[$];       // Containers of handles
   item         dh[];
   item         ah[string];
   int          byobj[item]; // Keyed by handle
   string       qs[$];
   real         qr[$];
   bit [7:0]    qb[$];
   logic [3:0]  aal[int];
   int          empty[$];
   event        ev;
   mailbox #(int) mb;
   mailbox #(int) mb_null;
   semaphore    sem;
   rec_t        rec;
   int          nest[][];

   function new();
      mb = new;
      sem = new(2);
   endfunction
endclass

module vpicls2;
   box bx;
   int mq[$];
   int mda[];
   string maa[string];
   item  mqh[$];

   initial begin
      static item a, b, c;
      a = new(10);
      b = new(20);
      c = new(30);
      bx = new;
      bx.di = new[3];
      bx.di[0] = 5; bx.di[1] = 6; bx.di[2] = 7;
      bx.qi = '{11, 22, 33, 44};
      bx.aai[3] = 30; bx.aai[-1] = 10; bx.aai[7] = 70;
      bx.aas["one"] = "uno"; bx.aas["two"] = "dos";
      bx.qh.push_back(a); bx.qh.push_back(b); bx.qh.push_back(null);
      bx.dh = new[2]; bx.dh[1] = c;
      bx.ah["a"] = a; bx.ah["c"] = c;
      bx.byobj[a] = 1; bx.byobj[c] = 3;
      bx.qs.push_back("x"); bx.qs.push_back("yz");
      bx.qr.push_back(1.5); bx.qr.push_back(-2.25);
      bx.qb.push_back(8'hA5); bx.qb.push_back(8'h0F);
      bx.aal[2] = 4'b1x0z;
      bx.rec.a = 9; bx.rec.s = "rs";
      bx.nest = new[2];
      bx.nest[0] = new[1]; bx.nest[0][0] = 100;
      bx.nest[1] = new[2]; bx.nest[1][0] = 200; bx.nest[1][1] = 201;
      bx.mb.put(1); bx.mb.put(2); bx.mb.put(3);

      mq = '{1, 2, 3};
      mda = new[2];
      mda[0] = -4; mda[1] = 4;
      maa["k"] = "v";
      mqh.push_back(c);
      #5;
      -> bx.ev;
      #5;
   end
endmodule
