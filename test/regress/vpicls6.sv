// Class handles that are the parameters and variables of a method, through
// vpi_nvc_frame_variables.  The C side is test/vpi/vpicls6.c; it needs
// --debug-stmts.

class item;
   int  v;
   item next;
   function new(int x);
      v = x;
   endfunction
endclass

class derived extends item;
   function new(int x);
      super.new(x);
   endfunction
endclass

class sink;
   item last;

   function void put(item it, item nothing);
      item loc;
      item none;
      loc = it;
      last = loc;
   endfunction
endclass

module vpicls6;
   sink s;
   initial begin
      static derived d;
      s = new;
      d = new(7);
      d.next = new(8);
      s.put(d, null);
   end
endmodule
