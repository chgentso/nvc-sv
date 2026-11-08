// IEEE 1800-2017 20.6.1: $typename gives a structure, union or
// enumeration with its body (members with their types, names with their
// values as sized literals, a base type other than int) followed by the
// name qualified by its scope, generates a name for an anonymous type,
// and qualifies a class by its scope: a module name followed by . or a
// package followed by ::.
package svtypename4_pkg;
  typedef enum logic [1:0] {PA, PB=3} pe_t;
  class Q; endclass
endpackage
module svtypename4;
  typedef enum {A, B, C=99} e_t;
  typedef struct packed { bit a; logic [3:0] b; } ps_t;
  typedef struct { int x; real y; string s; } us_t;
  typedef union tagged { int I; void N; } tu_t;
  enum {X1, X2} anon;
  class L; endclass
  class P #(int N = 4, type T = int); endclass
  e_t e; ps_t ps; us_t us; tu_t tu; svtypename4_pkg::pe_t pe;
  L l; P #(8, byte) p; svtypename4_pkg::Q q;
  int err = 0;

  task automatic check(string got, string exp);
    if (got != exp) begin
      $display("FAILED got [%s] expected [%s]", got, exp);
      err++;
    end
  endtask

  initial begin
    check($typename(e), "enum{A=32'sd0,B=32'sd1,C=32'sd99}svtypename4.e_t");
    check($typename(ps), "struct packed{bit a;logic[3:0] b;}svtypename4.ps_t");
    check($typename(us), "struct{int x;real y;string s;}svtypename4.us_t");
    check($typename(tu), "union tagged{int I;void N;}svtypename4.tu_t");
    check($typename(pe),
          "enum logic[1:0]{PA=2'd0,PB=2'd3}svtypename4_pkg::pe_t");
    check($typename(anon), "enum{X1=32'sd0,X2=32'sd1}svtypename4.e$1");
    check($typename(l), "class svtypename4.L");
    check($typename(p), "class svtypename4.P#(8,byte)");
    check($typename(q), "class svtypename4_pkg::Q");
    if (err == 0) $display("PASSED");
  end
endmodule
