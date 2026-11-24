class outer_c;
   int val;
   extern function inv(in);
   extern function int get_val;
   extern task set_val(int v);
   class inner_c;
      int x;
      extern function int get_x;
      extern function flip(a);
      extern task set_x(int v);
   endclass
endclass

function outer_c::inv(in);
   return ~in;
endfunction

function int outer_c::get_val;
   return val;
endfunction

task outer_c::set_val(int v);
   val = v;
endtask

function int outer_c::inner_c::get_x;
   return x;
endfunction

function outer_c::inner_c::flip(a);
   return ~a;
endfunction

task outer_c::inner_c::set_x(int v);
   x = v;
endtask

module vlog_extern_nest1;
   initial begin
      outer_c o = new;
      outer_c::inner_c i = new;
      o.set_val(7);
      i.set_x(11);
      if (o.get_val() !== 7 || o.inv(1'b0) !== 1'b1) $fatal(1, "FAIL outer");
      if (i.get_x() !== 11 || i.flip(1'b1) !== 1'b0) $fatal(1, "FAIL inner");
      $display("PASSED");
      $finish;
   end
endmodule
