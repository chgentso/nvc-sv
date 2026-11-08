module svmatches2;
  typedef union tagged { void Invalid; int Valid; } u_t;
  u_t u;
  int r;
  initial begin
    u = tagged Valid (6);
    r = u matches tagged Valid .v ? v + 1 : -1;
    if (r !== 7) $fatal(1, "r = %0d", r);
    u = tagged Invalid;
    r = u matches tagged Valid .v ? v + 1 : -1;
    if (r !== -1) $fatal(1, "r = %0d", r);
    $display("PASSED");
  end
endmodule
