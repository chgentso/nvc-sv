class box #(type T);
endclass

class wrap #(type T);
endclass

typedef wrap#(string) str_wrap_t;

class holder #(type T);
   typedef wrap#(string) str_wrap_t;
endclass

module vlog_typename_cls1;
   initial begin
      holder#(int)::str_wrap_t x[string];
      wrap#(string) y[string];
      if ($typename(box#(str_wrap_t)) != $typename(box#(wrap#(string))))
         $fatal(1, "FAIL 1: %s vs %s",
                $typename(box#(str_wrap_t)), $typename(box#(wrap#(string))));
      if ($typename(x) != $typename(y))
         $fatal(1, "FAIL 2: %s vs %s", $typename(x), $typename(y));
      $display("PASSED");
      $finish;
   end
endmodule
