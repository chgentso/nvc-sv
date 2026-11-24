module methargs1;
   class item;
   endclass

   class caller;
      function void f(input string s);
      endfunction
      function void g(input item it);
      endfunction
   endclass

   covergroup cg with function sample(int a, bit b = 1'b0);
      cp: coverpoint a;
   endgroup

   initial begin
      automatic caller c = new;
      automatic item it = new;
      automatic cg cov = new;
      automatic string str = "bad";
      c.f("ok");             // OK
      c.g(it);               // OK
      c.g(null);             // OK
      c.f(it);               // Error: class handle to string arg
      c.g(str);              // Error: string to class arg
      cov.sample(1);         // OK: b has default
      cov.sample();          // Error: too few args
      cov.sample(1, 0, 2);   // Error: too many args
   end
endmodule
