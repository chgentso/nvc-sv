// Expected values: IEEE 1800-2017 10.6.2 (force/release: after release a variable that has no other driver keeps the
// forced value until the next assignment), 10.4 (blocking assignment), 4.5 / 10.4.2 (nonblocking update occurs after the
// active region of the same time step), 9.4.1 (delay control)
`timescale 1ns/1ns
module svreleaseassign1;
  logic [7:0] v, w, x, y, z;
  int err = 0;
  function automatic void chk(bit ok, string m);
    if (!ok) begin $error("%s", m); err++; end
  endfunction
  initial begin
    v = 1; force v = 20; #1;
    chk(v === 20, "v forced");
    release v; v = 3;
    chk($time == 1 && v === 3, "v after release and assignment");
    #1 chk($time == 2 && v === 3, "v later");

    w = 1; force w = 21; #1;
    release w; chk($time == 3 && w === 21, "w keeps forced value after release");
    w = 4;
    chk(w === 4, "w assigned");
    #1 chk(w === 4, "w later");

    force x = 22; #1;
    chk(x === 22, "x forced");
    release x;
    chk(x === 22, "x keeps forced value after release");
    x = 5;
    chk(x === 5, "x assigned");
    #1 chk(x === 5, "x later");

    y = 2; force y = 23; #1;
    release y;
    y <= 6;
    chk(y === 23, "y keeps forced value until the nonblocking update");
    #1 chk(y === 6, "y nonblocking update");

    z = 7; force z = 24; #1;
    release z;
    #1 chk(z === 24, "z keeps forced value");
    z = 8;
    #1 chk(z === 8, "z assigned");
    if (err == 0) $display("PASSED");
  end
endmodule
