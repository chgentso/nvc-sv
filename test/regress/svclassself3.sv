// Test class members (property, function, task) having the same name as their class

module svclassself3;
  class CProp;
    int CProp;
    function int get();
      return CProp;
    endfunction
  endclass

  class CFunc;
    int val;
    function void CFunc(int x);
      val = x;
    endfunction
  endclass

  class CTask;
    task CTask(input int x, output int y);
      y = x + 3;
    endtask
  endclass

  initial begin
    CProp p = new;
    CFunc f = new;
    CTask t = new;
    int r;

    p.CProp = 23;
    if (p.get() !== 23 || p.CProp !== 23) $fatal(1, "CProp mismatch");

    f.CFunc(42);
    if (f.val !== 42) $fatal(1, "CFunc mismatch");

    t.CTask(39, r);
    if (r !== 42) $fatal(1, "CTask mismatch");

    $display("PASSED");
    $finish;
  end
endmodule
