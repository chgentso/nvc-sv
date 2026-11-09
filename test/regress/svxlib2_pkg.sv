package mypkg;
  typedef struct packed { logic [3:0] hi; logic [3:0] lo; } pair_t;

  class counter;
    int n;
    function void bump(int by);
      n += by;
    endfunction
  endclass

  function automatic int twice(int x);
    return 2 * x;
  endfunction
endpackage
