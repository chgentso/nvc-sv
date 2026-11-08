// IEEE 1800-2017 13.5.2: a ref argument needs a variable actual of an
// equivalent type and is not allowed in a static subroutine; 15.5.1,
// 15.5.2: the operand of -> and ->> is an event.
module refarg1;
  wire w;
  int v;
  logic [3:0] p;
  task automatic t(ref logic r); endtask
  task automatic t2(ref int r); endtask
  task t3(ref int r); endtask                   // Error
  function automatic void f(const ref int r); endfunction
  initial begin
    t(w);                                       // Error
    t2(v + 1);                                  // Error
    t2(p);                                      // Error
    f(v);                                       // OK
    ->> v;                                      // Error
    -> v;                                       // Error
  end
endmodule
