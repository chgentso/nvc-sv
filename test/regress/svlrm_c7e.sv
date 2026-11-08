// Self-checking: array indexing, slicing and manipulation.
// Expected values: IEEE 1800-2017 7.4.6 (out-of-range read gives the default value of the element type:
// 0 for 2-state int; out-of-range write has no effect), 7.6, 7.11, 11.5.1 (bit/part selects; out-of-range or x/z
// index of a 4-state vector reads x, writes are ignored), 10.9, 11.4.5 (unpacked array equality)
`define CHK(g, e) if ((g) !== (e)) begin errs++; $error("check failed at line %0d", `__LINE__); end
module svlrm_c7e;
  int a[8];
  logic [31:0] w;
  bit [15:0] arr2 [3];
  int idx; int errs;
  initial begin
    foreach (a[i]) a[i] = i * i;
    `CHK(a[0], 0) `CHK(a[7], 49)
    idx = 3; `CHK(a[idx], 9)
    idx = 9; `CHK(a[idx], 0)
    idx = -1; `CHK(a[idx], 0)
    `CHK(a['x], 0)
    a[8] = 5; `CHK(a[0], 0) `CHK(a[7], 49)
    w = 32'h12345678;
    `CHK(w[15:8], 8'h56) `CHK(w[3 +: 8], 8'hcf)
    `CHK(w[31 -: 8], 8'h12)
    idx = 4; `CHK(w[idx +: 4], 4'h7) `CHK(w[idx*2 +: 8], 8'h56)
    idx = 40; `CHK(w[idx], 1'bx)
    `CHK(w[-1], 1'bx) `CHK(w['x], 1'bx)
    `CHK(w[idx +: 4], 4'bxxxx)
    w[idx] = 1'b1; `CHK(w, 32'h12345678)
    w[3:0] = 4'hf; `CHK(w, 32'h1234567f)
    w[idx +: 4] = 4'hf; `CHK(w, 32'h1234567f)
    arr2[0] = 16'hbeef; arr2[1] = 16'hcafe; arr2[2] = 16'hf00d;
    `CHK(arr2[1][7:0], 8'hfe) `CHK(arr2[2][15:12], 4'hf)
    `CHK(arr2[0][3], 1'b1)
    begin
      automatic int s[3] = '{1,2,3}; int t[3];
      t = s; s[0] = 9; `CHK(t[0], 1) `CHK(s[0], 9)
      `CHK(t == s, 0)
      t = s; `CHK(t == s, 1)
    end
    begin
      automatic int s[2][2] = '{'{1,2},'{3,4}}; int t[2][2];
      t = s; `CHK(t[1][0], 3)
      t[1] = s[0]; `CHK(t[1][0], 1) `CHK(t[1][1], 2)
    end
    begin automatic int s[4] = '{1,2,3,4}; int d[]; d = s; `CHK(d.size(), 4) `CHK(d[2], 3) end
    begin
      automatic int s[4] = '{4,3,2,1}; int q[$];
      q = s; `CHK(q.size(), 4) `CHK(q[0], 4) `CHK(q[1], 3) `CHK(q[2], 2) `CHK(q[3], 1)
    end
    begin int u[2:0]; u = '{1, 2, 3}; `CHK(u[2], 1) `CHK(u[1], 2) `CHK(u[0], 3) end
    begin
      automatic logic [3:0][7:0] p = 32'hdeadbeef;
      `CHK(p[3], 8'hde) `CHK(p[0], 8'hef)
      p[1] = 8'h0; `CHK(p, 32'hdead00ef)
    end
    if (errs == 0) $display("PASSED");
  end
endmodule
