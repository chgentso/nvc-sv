// Expected values: IEEE 1800-2017 6.22.1 (matching types), 6.22.2 (equivalent types), 6.23 (type operator),
// 12.5 (case with type operands), 6.20.3 (type parameters)
// expT/expU encode which type each instance's T and U parameters hold:
//   "l12" = logic[11:0], "b4" = bit[3:0], "int" = int, "byte" = byte
module svtyperef1_t #(parameter type T = type(logic[11:0]), parameter type U = int,
                      parameter string expT = "l12", parameter string expU = "int") (output int nerr);
  typedef logic [3:0] nib_t;
  typedef nib_t alias_t;
  typedef struct { int a; } s1_t;
  typedef struct { int a; } s2_t;
  typedef enum { A, B } e_t;
  real r; string str; bit signed [7:0] sb; logic [7:0] lb; nib_t nb; alias_t ab;
  s1_t s1; s2_t s2; e_t e;
  initial nerr = 0;
  function automatic void chk(string name, bit got, bit exp);
    if (got !== exp) begin nerr++; $error("%m: %s: got %0d expected %0d", name, got, exp); end
  endfunction
  string tc, uc;
  initial begin
    case (type(T))
      type(logic[11:0]) : tc = "l12";
      default           : tc = "other";
    endcase
    case (type(U))
      type(logic[11:0]) : uc = "l12";
      type(int)         : uc = "int";
      default           : uc = "other";
    endcase
    if (expT == "l12") chk("case T", tc == "l12", 1); else chk("case T other", tc == "other", 1);
    if (expU == "l12" || expU == "int") chk("case U", uc == expU, 1); else chk("case U other", uc == "other", 1);
    chk("== wrong width", type(T) == type(logic[12:0]), 0);
    chk("!= right", type(T) != type(logic[11:0]), expT != "l12");
    chk("=== wrong width", type(T) === type(logic[12:0]), 0);
    chk("!== right", type(T) !== type(logic[11:0]), expT != "l12");
    chk("alias", type(nib_t) == type(alias_t), 1);
    chk("alias2", type(nb) == type(ab), 1);
    chk("vec", type(nb) == type(logic[3:0]), 1);
    chk("bit-logic", type(bit[3:0]) == type(logic[3:0]), 0);
    chk("reg", type(reg[3:0]) == type(logic[3:0]), 1);
    chk("sign", type(sb) == type(bit[7:0]), 0);
    chk("sign2", type(sb) == type(bit signed [7:0]), 1);
    chk("real", type(r) == type(real), 1);
    chk("str", type(str) == type(string), 1);
    chk("int", type(int) == type(integer), 0);
    chk("struct", type(s1) == type(s1_t), 1);
    chk("struct2", type(s1) == type(s2), 0);
    chk("enum", type(e) == type(e_t), 1);
    chk("expr", type(lb + lb) == type(logic[7:0]), 1);
    chk("T", type(T) == type(logic[11:0]), expT == "l12");
    chk("U", type(U) == type(int), expU == "int");
    chk("TU", type(T) == type(U), 0);
  end
endmodule

module svtyperef1;
  int n1, n2, n3;
  svtyperef1_t i1 (.nerr(n1));
  svtyperef1_t #(.T(bit [3:0]), .U(logic [11:0]), .expT("b4"), .expU("l12")) i2 (.nerr(n2));
  svtyperef1_t #(int, byte, "int", "byte") i3 (.nerr(n3));
  initial begin
    #1;
    if (n1 + n2 + n3 == 0) $display("PASSED");
  end
endmodule
