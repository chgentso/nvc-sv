// An unpacked array of dynamic arrays and of queues as a member of a
// structure variable
typedef struct { int m[3][]; int q[2][$]; } mx_t;

module svstructdynarr1;
  mx_t s;
  int bad;
  initial begin
    foreach (s.m[i]) s.m[i] = new[i + 1];
    foreach (s.m[i, j]) s.m[i][j] = 10 * i + j;
    s.q[1].push_back(7);
    s.q[1].push_back(8);
    if (s.m[0].size() != 1 || s.m[1].size() != 2 || s.m[2].size() != 3) bad++;
    foreach (s.m[i, j]) if (s.m[i][j] != 10 * i + j) bad++;
    if (s.q[0].size() != 0 || s.q[1].size() != 2 || s.q[1][1] != 8) bad++;
    if (bad == 0) $display("PASSED");
    else $display("FAILED %0d", bad);
  end
endmodule
