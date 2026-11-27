interface vlog_vifcmp1_if; int value; endinterface
module vlog_vifcmp1;
  vlog_vifcmp1_if m(), n();
  virtual vlog_vifcmp1_if h;
  initial begin
    h = n;
    $display("%0d %0d %0d %0d", h == n, h == m, m != h, n === h);
    h = null;
    $display("%0d", h == n);
  end
endmodule
