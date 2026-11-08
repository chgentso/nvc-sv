// Self-checking. Expected values: IEEE 1800-2017 10.10 (unpacked array concatenation: operands that are
// elements or unpacked arrays are concatenated left to right, usable as an argument whose formal is an
// unpacked array), 6.19 (enum values A=0,B=1,C=2 with a 5-bit base type), 7.10 (queue push_back).
`define CHK(tag, got, exp) if ((got) !== (exp)) begin errs++; $error("%s: got %0d expected %0d", tag, got, exp); end
`define CHKS(tag, got, exp) if ((got) != (exp)) begin errs++; $error("%s: got '%s' expected '%s'", tag, got, exp); end
typedef enum bit [4:0] {A, B, C} reg_t;
module svarrcat1;
  int errs = 0;
  reg_t gp[3];
  function automatic void f(reg_t r[2], int k, ref string q[$]);
    q.push_back($sformatf("%0d %0d %0d", r[0], r[1], k));
  endfunction
  function automatic void h(reg_t r[5], ref string q[$]);
    string s;
    foreach (r[i]) s = {s, $sformatf("%0d ", r[i])};
    q.push_back(s);
  endfunction
  function automatic int g(int v[3]);
    return v[0] * 100 + v[1] * 10 + v[2];
  endfunction
  initial begin
    string q[$];
    automatic reg_t x = C, y = B;
    automatic int a = 4;
    automatic int arr[2] = '{8, 9};
    gp = '{A, B, C};
    f({x, y}, 1, q);
    f({A, x}, 2, q);
    h({gp, x, y}, q);
    h({x, gp, y}, q);
    `CHK("q.size", q.size(), 4)
    `CHKS("q[0]", q[0], "2 1 1")
    `CHKS("q[1]", q[1], "0 2 2")
    `CHKS("q[2]", q[2], "0 1 2 2 1 ")
    `CHKS("q[3]", q[3], "2 0 1 2 1 ")
    `CHK("g({a,a+1,7})", g({a, a + 1, 7}), 457)
    `CHK("g({arr,a})", g({arr, a}), 894)
    if (errs == 0) $display("PASSED");
  end
endmodule
