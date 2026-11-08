package p1;
  parameter int W = 8;
  typedef logic [W-1:0] word_t;
  typedef enum {A, B, C} e_t;
  int counter = 0;
  function automatic int inc(); counter++; return counter; endfunction
  class K; int v = 3; static int sv = 7; function int get(); return v + W; endfunction endclass
  localparam string NAME = "p1";
  task automatic bump(int n); counter += n; endtask
endpackage
package p2;
  import p1::*;
  export p1::word_t;
  parameter int W2 = W * 2;
  function automatic word_t twice(word_t x); return x << 1; endfunction
endpackage
package p3;
  int counter = 100;
  parameter int W = 99;
endpackage
module svlrm_c26a;
  // Self-checking test. Expected values: IEEE 1800-2017 26.2 (package items: parameters, types,
  // enums, variables, functions, tasks, classes, localparams), 26.3 (wildcard and explicit import;
  // an explicit import takes precedence over a wildcard-imported name; an inner scope's wildcard
  // import is found before an outer one), 26.5 (export), 26.3 (scope resolution p::name, class
  // static member p1::K::sv), 20.6.2 ($bits), 6.19.5.2 (enum name()).
  import p1::*;
  import p3::counter;
  import p2::twice;
  word_t w;
  e_t e;
  K k;
  int errs, c1, c2;
  function automatic void ck(bit ok, string m);
    if (!ok) begin errs++; $error("%s", m); end
  endfunction
  initial begin
    w = 8'hf0; ck(w === 8'hf0 && $bits(word_t) == 8, "imported type");
    e = B; ck(e.name() == "B", "imported enum");
    c1 = inc(); c2 = inc(); ck(c1 == 1 && c2 == 2, "imported function updates package variable");
    ck(p1::counter == 2, "p1::counter");
    ck(counter == 100, "explicit import of p3::counter wins over p1::*");
    k = new; ck(k.get() == 11 && p1::K::sv == 7, "package class and static member");
    bump(5); ck(p1::counter == 7, "imported task");
    ck(twice(8'h81) === 8'h02, "function of package p2 using exported type");
    ck(p2::W2 == 16 && p3::W == 99, "package parameters");
    ck(p1::NAME == "p1", "package localparam");
    begin import p3::*; ck(W == 99, "inner scope wildcard import"); end
    ck(W == 8, "outer wildcard import");
    p3::counter = 5; ck(counter == 5 && p3::counter == 5, "write through p3::counter");
    if (errs == 0) $display("PASSED");
  end
endmodule
