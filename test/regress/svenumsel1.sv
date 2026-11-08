// Bit and part selects of enum variables and class enum members.
// Expected values: IEEE 1800-2017 6.19 (an enum is integral and takes its base type's
// value and width), 11.5.1 (bit-select and part-select).
`define CHK(c, m) if (!(c)) begin err++; $error(m); end
module svenumsel1;
  typedef enum bit [3:0] {S0=1, S1=2, S2=4, S3=8} sl_e;
  typedef enum {A=1, B=6, C=9} si_e;
  typedef enum logic [7:0] {L0=8'h0f, L1=8'hf0} lg_e;
  class tx;
    rand sl_e p;
    rand si_e q;
    lg_e l;
  endclass
  sl_e v;
  si_e w;
  lg_e u;
  int err = 0;
  initial begin
    automatic tx x = new;
    automatic int idx = -1;
    x.p = S2; v = S1; x.q = B; w = C; x.l = L1; u = L0;
    for (int i = 0; i < 4; i++) begin
      if (x.p[i] == 1) begin idx = i; break; end
    end
    `CHK(idx == 2, "lowest set bit of S2")
    `CHK(v[1] === 1'b1, "v[1]")
    `CHK(v[3:1] === 3'b001, "v[3:1]")
    `CHK(x.p[3:2] === 2'b01, "x.p[3:2]")
    `CHK(x.q[1] === 1'b1, "x.q[1]")
    `CHK(x.q[2:1] === 2'b11, "x.q[2:1]")
    `CHK(w[0] === 1'b1, "w[0]")
    `CHK(w[3:0] === 4'b1001, "w[3:0]")
    `CHK(x.l[7] === 1'b1, "x.l[7]")
    `CHK(x.l[7:4] === 4'b1111, "x.l[7:4]")
    `CHK(u[3] === 1'b1, "u[3]")
    `CHK(u[7:4] === 4'b0000, "u[7:4]")
    `CHK(v[1] && !v[0] && v[3:2] == 0, "ok1 condition")
    `CHK(x.l[7:4] == 4'hf, "ok2 condition")
    if (err == 0) $display("PASSED");
  end
endmodule
