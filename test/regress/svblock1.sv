// foreach over associative arrays with a blocking delay inside the loop body (self-checking).
// Expected values: IEEE 1800-2017 12.7.3 (foreach over an associative array visits the indices in index order:
// strings lexicographically, integrals numerically), 9.4.1 (#1 inside the loop advances time), 7.8.
// Reference model: a = {x:1, y:2, z:3}, b = {5:50, 1:10, 9:90}; a is visited x,y,z at t=1,2,3, then b is
// visited 1,5,9 at t=4,5,6; the sum of all values is 156.
`timescale 1ns/1ns
module svblock1;
  int a[string]; int b[int]; int n = 0;
  string ka[$]; int kb[$]; longint ta[$], tb[$];
  int errs = 0;
  initial begin
    a["x"]=1; a["y"]=2; a["z"]=3; b[5]=50; b[1]=10; b[9]=90;
    foreach (a[k]) begin #1; n += a[k]; ka.push_back(k); ta.push_back($time); end
    foreach (b[k]) begin #1; n += b[k]; kb.push_back(k); tb.push_back($time); end
    if (n != 156) begin errs++; $error("sum = %0d, expected 156", n); end
    if (ka.size() != 3 || ka[0] != "x" || ka[1] != "y" || ka[2] != "z") begin errs++; $error("string key order"); end
    if (ta.size() != 3 || ta[0] != 1 || ta[1] != 2 || ta[2] != 3) begin errs++; $error("string key times"); end
    if (kb.size() != 3 || kb[0] != 1 || kb[1] != 5 || kb[2] != 9) begin errs++; $error("int key order"); end
    if (tb.size() != 3 || tb[0] != 4 || tb[1] != 5 || tb[2] != 6) begin errs++; $error("int key times"); end
    if (errs == 0) $display("PASSED");
  end
endmodule
