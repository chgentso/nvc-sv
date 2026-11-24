module vlog_pattern_cmp1;
  int arr4[4];
  typedef int arr4_t[4];
  int q[$];

  typedef struct packed {
    logic [7:0] a;
    logic [7:0] b;
  } pair_t;

  pair_t p1;
  pair_t parr[2];
  byte byte_arr[2];

  initial begin
    arr4 = '{10, 20, 30, 40};
    if (!(arr4 == '{10, 20, 30, 40})) $fatal(1, "arr4 == pat failed");
    if (!(arr4 != '{10, 20, 30, 99})) $fatal(1, "arr4 != pat failed");
    if (!('{10, 20, 30, 40} == arr4)) $fatal(1, "pat == arr4 failed");
    if (!('{10, 20, 30, 99} != arr4)) $fatal(1, "pat != arr4 failed");
    if (arr4 == '{99, 99, 99, 99}) $fatal(1, "arr4 == wrong pat matched");

    q = '{20, 30, 40};
    if (!(q == '{20, 30, 40})) $fatal(1, "q == pat failed");
    if (!(q != '{20, 30, 99})) $fatal(1, "q != pat failed");
    if (!('{20, 30, 40} == q)) $fatal(1, "pat == q failed");

    p1 = '{a: 8'hAA, b: 8'h55};
    if (!(p1 == '{a: 8'hAA, b: 8'h55})) $fatal(1, "p1 == pat failed");
    if (!(p1 != '{a: 8'hAA, b: 8'hFF})) $fatal(1, "p1 != pat failed");
    if (!('{a: 8'hAA, b: 8'h55} == p1)) $fatal(1, "pat == p1 failed");

    parr[0] = '{a: 8'h01, b: 8'h02};
    parr[1] = '{a: 8'h03, b: 8'h04};
    if (!(parr == '{'{a: 8'h01, b: 8'h02}, '{a: 8'h03, b: 8'h04}}))
      $fatal(1, "parr == nested pat failed");
    if (!('{'{a: 8'h01, b: 8'h02}, '{a: 8'h03, b: 8'h04}} == parr))
      $fatal(1, "nested pat == parr failed");

    byte_arr = '{8'hAB, 8'hCD};
    if (!(byte_arr == '{8'hAB, 8'hCD})) $fatal(1, "byte_arr == pat failed");

    if (!(arr4_t'{10, 20, 30, 40} == arr4_t'{10, 20, 30, 40}))
      $fatal(1, "typed pat == typed pat failed");
    if (!(arr4_t'{10, 20, 30, 40} != arr4_t'{10, 20, 30, 99}))
      $fatal(1, "typed pat != typed pat failed");

    $display("PASSED");
    $finish;
  end
endmodule
