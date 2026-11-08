class c;
  int n;
  function int m(int a);
    n += a;
    return n;
  endfunction
  function void v(int a);
    m(a);
    this.m(a);
  endfunction
endclass

module svfunstmt1;
  int n;
  c obj = new;

  function int F(int a);
    n = n + a;
    return a + 1;
  endfunction

  task t;
    F(10);
  endtask

  initial begin
    F(3);
    if (n !== 3) $fatal(1, "n = %0d", n);
    t;
    if (n !== 13) $fatal(1, "n = %0d", n);
    obj.m(5);
    obj.v(2);
    if (obj.n !== 9) $fatal(1, "obj.n = %0d", obj.n);
    $display("PASSED");
  end
endmodule
