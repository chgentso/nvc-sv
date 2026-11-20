module svdynclass1;
  class Base;
    int id;
    function new(int i); id = i; endfunction
    virtual function int val(); return id; endfunction
  endclass

  class Derived extends Base;
    function new(int i); super.new(i); endfunction
    virtual function int val(); return id * 10; endfunction
  endclass

  class Holder;
    Derived items[3];
    int sum;

    function void run();
      foreach (items[i]) items[i] = new(i + 1);
      check(items);
    endfunction

    function void check(Base arr[]);
      sum = 0;
      foreach (arr[i]) sum += arr[i].val();
    endfunction
  endclass

  initial begin
    automatic Holder h = new;
    h.run();
    if (h.sum !== 60) $fatal(1, "got %0d", h.sum);
    $display("PASSED");
    $finish;
  end
endmodule
