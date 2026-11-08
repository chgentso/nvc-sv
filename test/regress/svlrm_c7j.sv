// Self-checking: streaming concatenation (pack / unpack, slice size, bit reversal).
// Expected values: IEEE 1800-2017 11.4.14 (>> keeps order, << reverses the order of slices of the given size,
// default one bit; first stream item is the most significant on pack; unpack fills left to right), 11.4.14.4
// (streaming strings and unpacked arrays)
`define CHK(g, e) if ((g) !== (e)) begin errs++; $error("check failed at line %0d", `__LINE__); end
module svlrm_c7j;
  logic [7:0] a = 8'hab;
  bit [15:0] w;
  byte b[4];
  int errs;
  initial begin
    w = {>>{a, 8'hcd}}; `CHK(w, 16'habcd)
    w = {<<{a, 8'hcd}}; `CHK(w, 16'hb3d5)
    w = {<<4{a, 8'hcd}}; `CHK(w, 16'hdcba)
    w = {<<8{a, 8'hcd}}; `CHK(w, 16'hcdab)
    {>>{b}} = 32'h01020304; `CHK(b[0], 1) `CHK(b[1], 2) `CHK(b[2], 3) `CHK(b[3], 4)
    {<<8{b}} = 32'h01020304; `CHK(b[0], 4) `CHK(b[1], 3) `CHK(b[2], 2) `CHK(b[3], 1)
    begin bit [31:0] x; x = {>>{b}}; `CHK(x, 32'h04030201) x = {<<{b}}; `CHK(x, 32'h8040c020) end
    begin bit [7:0] x, y; {x, y} = {<<{16'h1234}}; `CHK(x, 8'h2c) `CHK(y, 8'h48) end
    begin bit [3:0] n [2]; {>>{n}} = 8'h5a; `CHK(n[0], 4'h5) `CHK(n[1], 4'ha) end
    begin
      int u[2]; byte c[8];
      u = '{32'h11223344, 32'h55667788}; {>>{c}} = {>>{u}};
      `CHK(c[0], 8'h11) `CHK(c[7], 8'h88)
    end
    begin logic [2:0] t; t = {<<{3'b110}}; `CHK(t, 3'b011) end
    begin automatic bit [7:0] z = 8'b1100_0101; bit [7:0] rev; rev = {<<{z}}; `CHK(rev, 8'b10100011) end
    begin automatic string s = "abcd"; bit [31:0] x; x = {>>{s}}; `CHK(x, 32'h61626364) end
    if (errs == 0) $display("PASSED");
  end
endmodule
