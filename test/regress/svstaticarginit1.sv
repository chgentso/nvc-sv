// The initial value of a static variable declared in a static task or
// function may refer to an argument of that subroutine.
// IEEE 1800-2017 6.21: the arguments of a static subroutine are static
// variables and a static variable with an initial value is initialised once,
// before time 0, so such an initialiser sees the argument's own initial value
// (the default value of its type, 6.8 Table 6-7), not the actual value of any
// call; the static variable then keeps its value across calls.  A variable
// declared automatic in the same subroutine is initialised on every call with
// the actual argument of that call.  A variable with an initial value and no
// lifetime in a static subroutine is static (nvc warns that 6.21 requires an
// explicit lifetime there).
module svstaticarginit1;
  int errors = 0;

  task automatic check(string what, logic [63:0] got, logic [63:0] exp);
    if (got !== exp) begin
      $display("%s: got %0h expected %0h", what, got, exp);
      errors++;
    end
  endtask

  // Explicitly static variable initialised from an int argument
  int ts_seen[$];
  task ts(input int arg);
    static int y = arg;
    ts_seen.push_back(y);
    y += 10;
  endtask

  // No lifetime: static by default
  int td_seen[$];
  task td(input int arg);
    int y = arg + 1;
    td_seen.push_back(y);
    y = y * 2;
  endtask

  // 4-state argument: its initial value is all x
  logic [3:0] tl_seen[$];
  task tl(input logic [3:0] arg);
    static logic [3:0] y = arg;
    tl_seen.push_back(y);
    y = arg;
  endtask

  // Static function: argument with a default value.  The default is the
  // value given to the formal when a call omits it (13.5.3); before time 0
  // the formal still holds the default value of its type.
  function int fs(int a, int b = 7);
    static int y = a + b;
    automatic int z = a + b;
    fs = y * 1000 + z;
    y = y + 1;
  endfunction

  // Automatic counterpart in a static task: initialised on every call
  int ta_seen[$];
  task ta(input int arg);
    automatic int y = arg * 3;
    ta_seen.push_back(y);
    y = -1;
  endtask

  // Automatic subroutine: a variable without lifetime is automatic
  function automatic int fa(int a);
    int y = a - 1;
    return y;
  endfunction

  // Output argument: a static variable initialised from an output formal
  // also sees that formal's initial value
  function void fo(output int o, input int i);
    static int y = o;
    o = y + i;
    y = 100;
  endfunction

  int o1, o2;

  initial begin
    ts(5); ts(6); ts(7);
    check("ts[0]", ts_seen[0], 0);
    check("ts[1]", ts_seen[1], 10);
    check("ts[2]", ts_seen[2], 20);

    td(3); td(4); td(5);
    check("td[0]", td_seen[0], 1);
    check("td[1]", td_seen[1], 2);
    check("td[2]", td_seen[2], 4);

    tl(4'h5); tl(4'ha); tl(4'h3);
    check("tl[0]", tl_seen[0], 4'bxxxx);
    check("tl[1]", tl_seen[1], 4'h5);
    check("tl[2]", tl_seen[2], 4'ha);

    check("fs(1)", fs(1), 0 * 1000 + 8);
    check("fs(2,3)", fs(2, 3), 1 * 1000 + 5);
    check("fs(9)", fs(9), 2 * 1000 + 16);

    ta(1); ta(2); ta(-4);
    check("ta[0]", ta_seen[0], 3);
    check("ta[1]", ta_seen[1], 6);
    check("ta[2]", ta_seen[2], -12);

    check("fa(5)", fa(5), 4);
    check("fa(-5)", fa(-5), -6);

    fo(o1, 3);
    fo(o2, 4);
    check("fo 1", o1, 3);
    check("fo 2", o2, 104);

    if (errors == 0) $display("PASSED");
  end
endmodule
