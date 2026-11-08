// Assigning a typedef of an unpacked array to a fixed size array.
// Expected values: IEEE 1800-2017 7.6 (array assignments between equivalent unpacked
// arrays copy element by element), 10.9.1 (array assignment pattern), 13.4 (function return).
`define CHK(c, m) if (!(c)) begin err++; $error(m); end
module svdynassign1;
  int a3 [3] = '{3{1}};
  typedef int t3 [3];
  t3 u;
  int err = 0;
  function automatic t3 ret(); ret = '{3{4}}; endfunction
  initial begin
    u = ret(); `CHK(u[0] == 4 && u[1] == 4 && u[2] == 4, "u = ret()")
    a3 = u; `CHK(a3[0] == 4 && a3[1] == 4 && a3[2] == 4, "a3 = u")
    a3 = '{7,8,9}; u = a3; `CHK(u[0] == 7 && u[1] == 8 && u[2] == 9, "u = a3")
    a3 = '{1,1,1};
    a3 = ret(); `CHK(a3[0] == 4 && a3[1] == 4 && a3[2] == 4, "a3 = ret()")
    if (err == 0) $display("PASSED");
  end
endmodule
