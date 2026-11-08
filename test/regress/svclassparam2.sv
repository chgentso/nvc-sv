typedef struct s_fwd;
typedef union u_fwd;
typedef enum e_fwd;
typedef t_fwd;
typedef t_fwd;

typedef struct { logic a; logic b; } s_fwd;
typedef union { logic a; logic b; } u_fwd;
typedef enum { Global = 2, Local = 3 } e_fwd;
typedef int t_fwd;

package mypkg;
  class ModuleType #(parameter A = 1);
    int v = A;
  endclass
endpackage

class Foo extends mypkg::ModuleType #(5);
  parameter N = 2;
  localparam M = N + 1;
  localparam type T = int;
  function int sum();
    T t = 3;
    return N + M + t;
  endfunction
endclass

module svclassparam2;
  mypkg::ModuleType#(3) x = new;
  typedef mypkg::ModuleType#(4) t4;
  t4 y = new;
  Foo f = new;
  s_fwd s;
  e_fwd e = Local;
  t_fwd t = 7;
  parameter int foo = 32'dx__;

  initial begin
    if (x.v !== 3 || y.v !== 4 || f.v !== 5) $fatal(1, "bad %0d %0d %0d", x.v, y.v, f.v);
    if (f.sum() !== 8) $fatal(1, "sum %0d", f.sum());
    if (e !== 3 || t !== 7) $fatal(1, "e %0d t %0d", e, t);
    $display("PASSED");
  end
endmodule
