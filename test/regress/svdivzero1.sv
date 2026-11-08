// Integer division and modulus by zero.
// Expected values: IEEE 1800-2017 11.4.3 (arithmetic operators: if the second operand
// of / or % is zero the result is x), 6.11.? / 6.24 (a 4-state value assigned to a
// 2-state variable has its x and z bits converted to 0), 11.8 (expression type: the
// result is 4-state if any operand is 4-state).
`define CHK(c, m) if (!(c)) begin err++; $error(m); end
module svdivzero1;
  int a = 5, z = 0, r; integer ia = 5, iz = 0, ir; logic [7:0] la = 5, lz = 0, lr; bit [7:0] ba = 5, bz = 0, br;
  longint lla = 5, llz = 0, llr;
  int err = 0;
  initial begin
    r = a / z; `CHK(r === 0, "int /")
    r = a % z; `CHK(r === 0, "int %")
    ir = ia / iz; `CHK(ir === 32'bx, "integer /")
    ir = ia % iz; `CHK(ir === 32'bx, "integer %")
    lr = la / lz; `CHK(lr === 8'bx, "logic /")
    lr = la % lz; `CHK(lr === 8'bx, "logic %")
    br = ba / bz; `CHK(br === 8'd0, "bit /")
    br = ba % bz; `CHK(br === 8'd0, "bit %")
    llr = lla / llz; `CHK(llr === 64'd0, "longint /")
    r = a / lz; `CHK(r === 0, "int / logic (stored in int)")
    `CHK((a / lz) === 32'bx, "int / logic is 4-state x")
    r = (a / z) + 1; `CHK(r === 0 || r === 1, "expr is a defined 2-state value")
    ir = (ia / iz) + 1; `CHK(ir === 32'bx, "x propagates through +")
    r = 10**9 / z; `CHK(r === 0, "const / 0")
    if (err == 0) $display("PASSED");
  end
endmodule
