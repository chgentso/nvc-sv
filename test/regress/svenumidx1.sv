// Self-checking. Expected values: IEEE 1800-2017 6.5 (a variable may be the
// target of several continuous assignments if they write different parts of it:
// a bit select whose index is a constant, here an enumeration name with and
// without an explicit value, 6.19.2, selects a different longest static prefix
// each time), 11.5.1 (bit-select).
package svenumidx1_pkg;
  typedef enum int unsigned { A0 = 0, A1 = 1 } idx_e;
  typedef enum int unsigned { B0, B1, B2 } seq_e;
  parameter int NumAlerts = 2;
endpackage

module svenumidx1;
  import svenumidx1_pkg::*;
  logic [NumAlerts-1:0] a;
  logic [2:0] b;
  logic x = 0, y = 0, z = 0;
  int errs = 0;
  assign a[A0] = x;
  assign a[A1] = y;
  assign b[B0] = x;
  assign b[B1] = y;
  assign b[B2] = z;
  initial begin
    #1 x = 1;
    #1 if (a !== 2'b01 || b !== 3'b001) begin errs++; $error("a=%b b=%b", a, b); end
    y = 1; z = 1;
    #1 if (a !== 2'b11 || b !== 3'b111) begin errs++; $error("a=%b b=%b", a, b); end
    x = 0;
    #1 if (a !== 2'b10 || b !== 3'b110) begin errs++; $error("a=%b b=%b", a, b); end
    if (errs == 0) $display("PASSED");
  end
endmodule
