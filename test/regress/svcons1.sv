// Constraint prototypes with the block defined outside of the class
class Packet;
   rand int len;
   rand bit [7:0] kind;
   rand int pad;

   extern constraint c_len;
   constraint c_kind;           // Prototype without extern
   extern static constraint c_pad;

   function new();
   endfunction
endclass

constraint Packet::c_len {
   len inside { [10:20] };
   len % 2 == 0;
}

constraint Packet::c_kind {
   kind > 200;
   kind != 250;
}

static constraint Packet::c_pad {
   pad == len + 1;
}

class Derived extends Packet;
   constraint c_len { len == 14; }   // Replaces the external constraint
endclass

module svcons1;
   Packet p;
   Derived d;

   initial begin
      p = new;
      for (int i = 0; i < 50; i++) begin
         if (!p.randomize() || p.len < 10 || p.len > 20 || p.len % 2 != 0
             || p.kind <= 200 || p.kind == 250 || p.pad != p.len + 1) begin
            $display("FAILED %0d %0d %0d", p.len, p.kind, p.pad);
            $finish;
         end
      end

      p.c_kind.constraint_mode(0);
      d = new;
      for (int i = 0; i < 20; i++) begin
         if (!d.randomize() || d.len != 14 || d.pad != 15) begin
            $display("FAILED derived %0d %0d", d.len, d.pad);
            $finish;
         end
      end

      $display("PASSED");
   end
endmodule
