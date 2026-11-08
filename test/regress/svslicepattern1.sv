// Assignment pattern for a slice of an array
// Expected values: IEEE 1800-2017 10.9.1 (array assignment patterns, replication, default), 7.4.3 (array slices)
module svslicepattern1;
  byte payload[0:127];
  int a[8];
  initial begin
    payload = '{default: 1};
    payload[0:123] = '{124{'h00}};
    payload[4] = 5;
    a = '{8{7}};
    a[2:4] = '{1, 2, 3};
    if (payload[0] == 0 && payload[4] == 5 && payload[123] == 0 && payload[124] == 1 && payload[127] == 1 &&
        a[0] == 7 && a[1] == 7 && a[2] == 1 && a[3] == 2 && a[4] == 3 && a[5] == 7 && a[7] == 7)
      $display("PASSED");
    else
      $error("slice pattern: %0d %0d %0d %0d | %0d %0d %0d %0d %0d", payload[0], payload[4], payload[123],
             payload[124], a[1], a[2], a[3], a[4], a[5]);
  end
endmodule
