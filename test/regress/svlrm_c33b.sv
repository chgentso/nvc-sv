module svlrm_c33b;
  // Self-checking test. Expected values: IEEE 1800-2017 20.18.1 ($system executes the string as a shell
  // command and returns the C system() result: 0 for success, and the exit status in bits 15:8 of the
  // wait status, so "false" (status 1) gives 256 and "exit 3" gives 768), 20.3 ($time, $stime,
  // $realtime are 0 at time 0). The output of the echoed command is not checked.
  int r;
  string s;
  int errs;
  function automatic void ck(bit ok, string m);
    if (!ok) begin errs++; $error("%s", m); end
  endfunction
  initial begin
    r = $system("true");   ck(r == 0, "true returns 0");
    r = $system("false");  ck(r == 256, "false returns exit status 1 in bits 15:8");
    r = $system("exit 3"); ck(r == 768, "exit 3 returns 3 << 8");
    void'($system("echo hello from shell"));
    ck($time == 0, "$time");
    ck($stime == 0, "$stime");
    ck($realtime == 0.0, "$realtime");
    if (errs == 0) $display("PASSED");
  end
endmodule
