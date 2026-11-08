// IEEE 1800-2017 6.21: variables of a static task or procedural block are
// static by default and the initial value of a static variable is assigned
// once at the start of simulation.  nvc only warns about a missing
// lifetime and keeps the variable static.
module svlifetime2;
  int g = 5;
  int errors = 0;

  task count(output int r);
    int n = 10;                 // Static: counts across calls
    n++;
    r = n;
  endtask

  initial g = 6;                // Runs after the initial values are set

  initial begin
    int r1, r2;
    int seen = g;               // Static: gets the initial value of g
    count(r1);
    count(r2);
    if (r1 != 11 || r2 != 12) begin
      $display("count gave %0d %0d, expected 11 12", r1, r2);
      errors++;
    end
    if (seen != 5) begin
      $display("seen = %0d, expected 5", seen);
      errors++;
    end
    if (errors == 0) $display("PASSED");
  end
endmodule
