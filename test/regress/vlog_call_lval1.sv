class box;
   int x;
   static box inst;
   static function box get_inst();
      if (inst == null) inst = new;
      return inst;
   endfunction
endclass

class holder;
   box b;
   function new();
      b = new;
   endfunction
   static function holder get_holder();
      static holder h = new;
      return h;
   endfunction
endclass

module vlog_call_lval1;
   initial begin
      box::get_inst().x = 10;
      if (box::get_inst().x !== 10) $fatal(1, "FAIL 1");

      box::get_inst().x += 5;
      if (box::get_inst().x !== 15) $fatal(1, "FAIL 2");

      box::get_inst().x++;
      if (box::get_inst().x !== 16) $fatal(1, "FAIL 3");

      holder::get_holder().b.x = 42;
      if (holder::get_holder().b.x !== 42) $fatal(1, "FAIL 4");

      $display("PASSED");
      $finish;
   end
endmodule
