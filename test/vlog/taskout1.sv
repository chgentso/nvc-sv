task automatic t(input int d, output logic [1:0] y, inout int z);
  y = d;
endtask

function automatic int f(input int d, output int y);
  y = d;
  return d;
endfunction

module taskout1;
  logic [1:0] y;
  int z, r;
  initial begin
    t(1, y, z);
    t(1, 2'b01, z);
    t(1, y, 5);
    t(1, y, z + 1);
    t(.d(1), .y(1), .z(z));
    r = f(1, 2);
    r = f(1, y);
  end
endmodule
