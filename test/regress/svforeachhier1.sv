// IEEE 1800-2017 12.7.3: the array of a foreach loop may be any array
// reference, including a hierarchical one (an interface instance member, a
// variable of a module instance named from the top), with one loop
// variable per dimension; the loop variables follow the index ranges of
// that array, including descending and non-zero-based ranges.
interface svforeachhier1_if;
  int q[$];
  int dyn[];
endinterface

module svforeachhier1_sub;
  int arr[2:0][1:4];
  string names[int];
endmodule

module svforeachhier1;
  svforeachhier1_if bi();
  svforeachhier1_sub u();

  initial begin
    int sum, cnt;
    string order;

    bi.q = '{10, 20, 30};
    bi.dyn = new[2];
    bi.dyn = '{7, 8};
    foreach (u.arr[i, j]) u.arr[i][j] = i * 10 + j;
    u.names[5] = "five";
    u.names[-1] = "minus";

    sum = 0; cnt = 0;
    foreach (bi.q[i]) begin
      sum += bi.q[i] * (i + 1);
      cnt++;
    end
    if (cnt != 3 || sum != 10 + 40 + 90)
      $fatal(1, "bi.q: cnt %0d sum %0d", cnt, sum);

    sum = 0;
    foreach (svforeachhier1.bi.dyn[k]) sum += bi.dyn[k] << k;
    if (sum != 7 + 16) $fatal(1, "bi.dyn: sum %0d", sum);

    // Order follows the declared ranges: i from 2 down to 0, j 1 to 4
    order = "";
    foreach (svforeachhier1.u.arr[i, j])
      order = {order, $sformatf("%0d%0d ", i, j)};
    if (order != "21 22 23 24 11 12 13 14 01 02 03 04 ")
      $fatal(1, "u.arr order: %s", order);

    sum = 0;
    foreach (u.arr[i, j]) sum += u.arr[i][j];
    // sum over i=0..2, j=1..4 of 10i+j = 4*10*(0+1+2) + 3*(1+2+3+4)
    if (sum != 120 + 30) $fatal(1, "u.arr sum %0d", sum);

    // Only the first dimension
    cnt = 0;
    foreach (u.arr[i]) cnt++;
    if (cnt != 3) $fatal(1, "u.arr first dimension: %0d", cnt);

    // Associative array: index order
    order = "";
    foreach (u.names[key]) order = {order, $sformatf("%0d:%s ", key,
                                                      u.names[key])};
    if (order != "-1:minus 5:five ") $fatal(1, "u.names: %s", order);

    $display("PASSED");
  end
endmodule
