// IEEE 1800-2017 13.4.4: a function cannot call a task except from a
// fork-join_none block (a background process); 13.4: functions cannot block
module functask1;
  task automatic t(); $display("t"); endtask
  function void f1();
    t();                                // Error
  endfunction
  function void f2();
    fork
      t();                              // OK
    join_none
  endfunction
  function int f3();
    t();                                // Error
    return 1;
  endfunction
endmodule
