// Self-checking. IEEE 1800-2017 7.3.1: a packed union is a vector of the size
// of its members and bits can be selected from it like a packed structure.
module svunionsel1;
  union packed {
    logic [3:0][7:0] idx;
    struct packed { logic [15:0] hi; logic [15:0] lo; } nam;
  } u;
  logic [7:0] b;
  logic [3:0] n;
  integer i;
  initial begin
    u = 32'h12345678;
    b = u[15:8];
    if (b !== 8'h56) $error("FAIL const %h", b);
    i = 8;
    b = u[i +: 8];
    if (b !== 8'h56) $error("FAIL indexed %h", b);
    n = u[3:0];
    if (n !== 4'h8) $error("FAIL low %h", n);
    if (u[31] !== 1'b0 || u[28] !== 1'b1) $error("FAIL bits");
    $display("PASSED");
  end
endmodule
