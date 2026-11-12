// An index wider than 32 bits that is far out of range of an unpacked array
module svwideidx1;
  function automatic integer f(input [63:0] i);
    int a[3] = '{default: 7};
    a[i] = 5;
    return a[i] + a[0] + a[1] + a[2];
  endfunction

  function automatic integer g(input [63:0] i);
    int a[3] = '{default: 7};
    return a[i];
  endfunction

  localparam integer P = f(64'h1_0000_0000);

  initial begin
    if (f(64'h1_0000_0000) !== 21) $fatal(1, "f far %0d", f(64'h1_0000_0000));
    if (f(64'h1) !== 24) $fatal(1, "f in range %0d", f(64'h1));
    if (g(64'h8000_0000_0000_0000) !== 0) $fatal(1, "g far");
    if (g(64'h2) !== 7) $fatal(1, "g in range");
    if (P !== 21) $fatal(1, "P %0d", P);
    $display("PASS");
  end
endmodule
