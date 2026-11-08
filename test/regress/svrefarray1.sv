// Expected values: IEEE 1800-2017 13.5.2 (pass by reference, const ref), 7.4 (unpacked arrays), 12.7.3 (foreach),
// 6.8 (8-bit wrap-around of bit [7:0])
module svrefarray1;
  int arr4[4];
  int arr2[2][2];
  bit [7:0] b3[3];
  int err = 0;
  function automatic void chk(bit ok, string m);
    if (!ok) begin $error("%s", m); err++; end
  endfunction
  function automatic void fill(ref int a[4]); foreach (a[i]) a[i] = i * i; endfunction
  function automatic int sum(const ref int a[4]); int s = 0; foreach (a[i]) s += a[i]; return s; endfunction
  function automatic void bump(ref bit [7:0] a[3]); foreach (a[i]) a[i] += 8'd1; endfunction
  task automatic tfill(ref int a[2][2]); a[0][0] = 1; a[0][1] = 2; a[1][0] = 3; a[1][1] = 4; endtask
  function automatic void local_use(); int l[4]; fill(l); chk(l[3] == 9 && sum(l) == 14, "local ref array"); endfunction
  initial begin
    fill(arr4);
    chk(arr4[0] == 0 && arr4[1] == 1 && arr4[2] == 4 && arr4[3] == 9, "fill");
    chk(sum(arr4) == 14, "sum");
    b3 = '{8'h10, 8'h20, 8'hff}; bump(b3);
    chk(b3[0] == 8'h11 && b3[1] == 8'h21 && b3[2] == 8'h00, "bump wraps");
    tfill(arr2);
    chk(arr2[0][0] == 1 && arr2[0][1] == 2 && arr2[1][0] == 3 && arr2[1][1] == 4, "2d ref");
    local_use();
    if (err == 0) $display("PASSED");
  end
endmodule
