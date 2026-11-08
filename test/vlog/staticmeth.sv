class staticmeth;
  task static t1();  // Error
  endtask
  function static int f1();  // Error
    return 1;
  endfunction
  static task t2();  // OK
  endtask
  task automatic t3();  // OK
  endtask
endclass
