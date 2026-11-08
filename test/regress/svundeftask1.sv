// Calling an undefined system task is an error that does not stop the simulation
module svundeftask1;
  int i, r;
  initial begin
    for (i = 0; i < 2; i++) $nosuchtask(i);
    $display("after tasks %0d", i);
    r = $nosuchfunc(3);
    $display("after func %0d", r);
  end
endmodule
