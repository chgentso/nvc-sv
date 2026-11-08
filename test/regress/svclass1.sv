// Classes: inheritance, virtual dispatch, constructors, statics, default args
virtual class base;
   static int count = 0;
   int        id;
   string     name;

   function new(int id, string name = "dflt");
      this.id = id;
      this.name = name;
      count++;
   endfunction

   pure virtual function int value();

   virtual function int twice();
      return value() * 2;
   endfunction

   function int get_id();
      return id;
   endfunction
endclass

class derived extends base;
   int extra = 7;

   function new(int id);
      super.new(id, "derived");
   endfunction

   function int value();
      return extra + id;
   endfunction
endclass

class other extends base;
   function new();
      super.new(100);
   endfunction

   function int value();
      return 1;
   endfunction

   virtual function int twice();
      return 555;
   endfunction
endclass

module svclass1;
   base b;
   derived d;
   initial begin
      d = new(3);
      b = d;
      if (b.value() != 10) $display("FAILED value %0d", b.value());
      if (b.twice() != 20) $display("FAILED twice %0d", b.twice());
      if (b.get_id() != 3) $display("FAILED id");
      if (b.name != "derived") $display("FAILED name");
      begin
         other o;
         o = new;
         b = o;
         if (b.twice() != 555) $display("FAILED other %0d", b.twice());
         if (b.name != "dflt") $display("FAILED dflt");
      end
      $display("PASSED");
   end
endmodule
