// Assignment patterns whose type is that of a selected part of a packed array
// Expected values: IEEE 1800-2017 10.9.1 (array assignment patterns), 10.8 (assignment-like contexts)
package svpackedpattern1_pkg;
  typedef struct packed { logic s; logic [2:0] m; } st_t;
  localparam st_t DC = '{default: 1'b1};
endpackage

module svpackedpattern1;
  import svpackedpattern1_pkg::*;
  logic [1:0][3:0] a;
  st_t [1:0][2:0] b;
  logic [1:0][3:0] c;
  logic [3:0] d;
  logic [7:0] e, f;
  logic x;
  logic [3:0] u [2];
  assign a[0] = '{default: x};
  assign b[1] = '{default: DC};
  assign c[1][1:0] = '{default: x};
  assign f[7:4] = '{default: ~x};
  always_comb begin
    d = '{default: x};
    e[3:0] = '{default: x};
    e[7:4] = '{default: ~x};
  end
  initial begin
    x = 1;
    #1;
    u[0] = '{default: x};
    u[1][2:1] = '{default: x};
    if (a[0] === 4'b1111 && b[1] === 12'b1001_1001_1001 && c[1][1:0] === 2'b11
        && f[7:4] === 4'b0000 && d === 4'b1111 && e === 8'b0000_1111
        && u[0] === 4'b1111 && u[1][2:1] === 2'b11)
      $display("PASSED");
    else
      $error("packed pattern: %b %b %b %b %b %b %b %b", a[0], b[1], c[1][1:0],
             f[7:4], d, e, u[0], u[1][2:1]);
  end
endmodule
