module extern1;
  class A;
    int x;
    extern function int f(int a);
    extern task t(int a, int b);
    function int g();
      return 1;
    endfunction
    static int count;
  endclass

  function int A::f(int b);
    return 0;
  endfunction

  function int A::g();
    return 2;
  endfunction

  function int A::h();
    return 2;
  endfunction

  function int B::f(int a);
    return 1;
  endfunction

  task A::t(int a);
  endtask
endmodule
