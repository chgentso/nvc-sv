// IEEE 1800-2017 9.2.3: a final procedure can only contain what a function
// can, which includes no call of a task.
module finaltask1;
  task t;
  endtask
  function void f;
  endfunction
  final begin : nested
    t;                                          // Error
    f;                                          // OK
    $display("done");                           // OK
  end
endmodule
