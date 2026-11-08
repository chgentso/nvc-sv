// %m in subroutines called from another instance (hierarchical call)
// Expected values: IEEE 1800-2017 21.2.1.6 (%m prints the hierarchical name of
// the module, task, function or named block in which it is invoked), 23.6
// (hierarchical names), 27.5 (generate block names gg[0]).
module svhierm2_sub;
  function string f; return $sformatf("f: %m"); endfunction
  task t(output string r); r = $sformatf("t: %m"); endtask
  function string g; begin : in return $sformatf("g: %m"); end endfunction
endmodule
module svhierm2;
  svhierm2_sub s1(); svhierm2_sub s2();
  for (genvar i = 0; i < 2; i++) begin : gg svhierm2_sub s3(); end
  int errors = 0;
  task automatic chk(string got, string exp);
    if (got != exp) begin $error("got '%s' expected '%s'", got, exp); errors++; end
  endtask
  string r;
  initial begin
    chk(s1.f(), "f: svhierm2.s1.f");
    s2.t(r); chk(r, "t: svhierm2.s2.t");
    chk(s1.g(), "g: svhierm2.s1.g.in");
    chk(gg[1].s3.f(), "f: svhierm2.gg[1].s3.f");
    chk($sformatf("top %m"), "top svhierm2");
    if (errors == 0) $display("PASSED");
  end
endmodule
