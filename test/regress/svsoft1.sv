// disable soft discards the soft constraints which come before it
class a;
   rand int b;
   rand int c;

   constraint c1 {
      soft b > 4;
      soft b < 12;
      soft c == 3;
   }
   constraint c2 { disable soft b; }
   constraint c3 { soft b == 20; }
endclass

class d extends a;
   constraint c4 { disable soft c; soft c == 8; }
endclass

class e;
   rand int x;
   constraint k1 { soft x == 5; }
   constraint k2 { x inside { [1:10] }; disable soft x; }
endclass

module svsoft1;
   a obj;
   d dobj;
   e eobj;
   int others;

   initial begin
      obj = new;
      for (int i = 0; i < 10; i++) begin
         if (!obj.randomize() || obj.b != 20 || obj.c != 3) begin
            $display("FAILED a: %0d %0d", obj.b, obj.c);
            $finish;
         end
      end

      dobj = new;
      for (int i = 0; i < 10; i++) begin
         if (!dobj.randomize() || dobj.b != 20 || dobj.c != 8) begin
            $display("FAILED d: %0d %0d", dobj.b, dobj.c);
            $finish;
         end
      end

      // The soft constraint x == 5 is discarded so other values are chosen
      eobj = new;
      for (int i = 0; i < 100; i++) begin
         if (!eobj.randomize() || eobj.x < 1 || eobj.x > 10) begin
            $display("FAILED e: %0d", eobj.x);
            $finish;
         end
         if (eobj.x != 5)
           others++;
      end

      if (others == 0) begin
         $display("FAILED soft constraint was not discarded");
         $finish;
      end

      $display("PASSED");
   end
endmodule
