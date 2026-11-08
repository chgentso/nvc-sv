module svfmtp1;
  int i = 42;
  string s;
  initial begin
    s = $sformatf("%0p", i);
    if (s != "42") $fatal(1, "s = %s", s);
    $display("PASSED");
  end
endmodule
