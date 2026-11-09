// Expected values: IEEE 1800-2017 21.3.4.3 ($sscanf/$fscanf: %f %e %g read real values, %d accepts '_', %s,
// return value = number of items assigned, 0 on a matching failure), 6.12 (real, shortreal)
module svscanreal1;
  real x, y, z; int n, m, r; string s; int fd; shortreal sr;
  int err = 0;
  function automatic void chk(bit ok, string msg);
    if (!ok) begin $error("%s", msg); err++; end
  endfunction
  initial begin
    r = $sscanf("1.5 2.25e2 -3", "%f %f %f", x, y, z); chk(r == 3 && x == 1.5 && y == 225.0 && z == -3.0, "three reals");
    r = $sscanf("abc 7.5", "%s %f", s, x); chk(r == 2 && s == "abc" && x == 7.5, "string then real");
    r = $sscanf("3.14", "%f", sr); chk(r == 1 && sr == shortreal'(3.14), "shortreal");
    r = $sscanf("x", "%f", x); chk(r == 0, "no match");
    r = $sscanf("1e3", "%e", x); chk(r == 1 && x == 1000.0, "%e");
    r = $sscanf("2.5 10", "%g %d", x, n); chk(r == 2 && x == 2.5 && n == 10, "%g %d");
    r = $sscanf("1_000 1_0", "%d %d", n, m); chk(r == 2 && n == 1000 && m == 10, "underscores in %d");
    fd = $fopen("svscanreal1.txt", "w"); $fdisplay(fd, "0.5 7 1.25e-1"); $fclose(fd);
    fd = $fopen("svscanreal1.txt", "r");
    r = $fscanf(fd, "%f %d %f", x, n, y); chk(r == 3 && x == 0.5 && n == 7 && y == 0.125, "fscanf");
    $fclose(fd);
    if (err == 0) $display("PASSED");
  end
endmodule
