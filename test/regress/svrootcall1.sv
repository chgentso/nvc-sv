// Hierarchical subroutine calls and assignments rooted at $root
// IEEE 1800-2017 23.3.1, 23.6
module svrootcall1_sub;
  int count = 0;
  task automatic inc(input int delta);
    count += delta;
  endtask
  function automatic int add_and_get(input int delta);
    count += delta;
    return count;
  endfunction
endmodule

module svrootcall1;
  svrootcall1_sub sub();
  int direct = 0;

  initial begin
    $root.svrootcall1.sub.inc(3);
    void'($root.svrootcall1.sub.add_and_get(4));
    $root.svrootcall1.direct = 10;
    if (sub.count === 7 && direct === 10)
      $display("PASSED");
    else
      $error("expected 7 and 10, got %0d and %0d", sub.count, direct);
  end
endmodule
