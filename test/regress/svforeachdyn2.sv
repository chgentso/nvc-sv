// IEEE 1800-2017 12.7.3: foreach over arrays with an inner dynamic dimension
// named hierarchically, including by an instance declared later
module svforeachdyn2_sub;
  int aq[2][$];
  int d[][];
  int q[$][3];
  int e[$][$];
  initial begin
    aq[0].push_back(1); aq[0].push_back(2); aq[1].push_back(5);
    d = new[2]; d[0] = new[3]; d[1] = new[1];
    q.push_back('{1, 2, 3}); q.push_back('{4, 5, 6});
    e.push_back({1}); e.push_back({2, 3});
  end
endmodule

module svforeachdyn2;
  svforeachdyn2_sub u();
  int sum;
  initial begin
    #1;
    $display("%0d %0d %0d", u.aq[0].size(), u.aq[1].size(), u.aq[0][1]);
    $display("%0d %0d", $size(u.aq[0]), $size(u.aq[1]));
    foreach (u.aq[i, j]) begin sum += u.aq[i][j]; $display("aq %0d %0d", i, j); end
    foreach (u.d[i, j]) $display("d %0d %0d", i, j);
    foreach (u.q[i, j]) sum += u.q[i][j];
    $display("sum %0d", sum);
    foreach (fwd.aq[i, j]) $display("fwd aq %0d %0d", i, j);
    foreach (fwd.d[i, j]) $display("fwd d %0d %0d", i, j);
    foreach (fwd.q[i, j]) $display("fwd q %0d %0d", i, j);
    foreach (fwd.e[i, j]) $display("fwd e %0d %0d", i, j);
  end
  svforeachdyn2_sub fwd();
endmodule
