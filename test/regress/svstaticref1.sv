// Expected values: IEEE 1800-2017 6.21 (a static variable declared in a subroutine keeps its value across calls,
// its initializer is applied once; one copy shared by all objects for class methods),
// 8.9/8.10 (class methods), 23.6 (hierarchical name of a variable inside a subroutine),
// 11.4.12 (string concatenation)
class c;
  static int last_n;
  static string last_s;
  function void f();
    static int n;
    n++;
    last_n = n;
  endfunction
  task t();
    static string s = "init";
    last_s = s;
    s = {s, "+"};
  endtask
  function void reset();
    f.n = 10;
    t.s = "reset";
  endfunction
  function void show();
    last_n = f.n;
    last_s = t.s;
  endfunction
endclass

module svstaticref1;
  int errs = 0;
  task automatic ci(string w, int got, int exp);
    if (got != exp) begin $error("%s: got %0d expected %0d", w, got, exp); errs++; end
  endtask
  task automatic cs(string w, string got, string exp);
    if (got != exp) begin $error("%s: got \"%s\" expected \"%s\"", w, got, exp); errs++; end
  endtask

  function int count();
    static int calls = 0;
    calls++;
    return calls;
  endfunction

  task tick();
    static int ticks;
    ticks++;
  endtask

  c a = new, b = new;
  initial begin
    void'(count());
    void'(count());
    tick(); tick(); tick();
    ci("count.calls", count.calls, 2);
    ci("tick.ticks", tick.ticks, 3);
    count.calls = 40;
    ci("count()", count(), 41);
    a.f(); ci("a.f", c::last_n, 1);
    b.f(); ci("b.f shares n", c::last_n, 2);
    a.t(); cs("a.t", c::last_s, "init");
    b.t(); cs("b.t shares s", c::last_s, "init+");
    a.show();
    ci("show f.n", c::last_n, 2);
    cs("show t.s", c::last_s, "init++");
    a.reset();
    b.f(); ci("b.f after reset", c::last_n, 11);
    b.t(); cs("b.t after reset", c::last_s, "reset");
    b.show();
    ci("show f.n 2", c::last_n, 11);
    cs("show t.s 2", c::last_s, "reset+");
    if (errs == 0) $display("PASSED");
  end
endmodule
