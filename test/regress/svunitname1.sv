// Names of the compilation-unit scope in $typename and %m
// IEEE 1800-2017 3.12.1 and 26.3: the compilation-unit scope is called $unit; 20.6.1: a
// type is named with the scope that declares it, a package or class scope followed by
// :: ($unit::name is the form used to refer to a compilation-unit item, 26.3); 21.2.1.6:
// %m gives the hierarchical name of the subroutine or named block that contains the call.
typedef struct packed { logic a; } svunitname1_s;
class svunitname1_c;
  function string m(); return $sformatf("%m"); endfunction
endclass
function automatic string svunitname1_f(); return $sformatf("%m"); endfunction
task automatic svunitname1_t(output string s);
  begin : blk s = $sformatf("%m"); end
endtask
module svunitname1;
  int errs = 0;
  function automatic void check(string got, string exp);
    if (got != exp) begin
      $display("got '%s' expected '%s'", got, exp); errs++;
    end
  endfunction
  initial begin
    automatic svunitname1_c c = new; string s;
    check($typename(svunitname1_s), "struct packed{logic a;}$unit::svunitname1_s");
    check($typename(c), "class $unit::svunitname1_c");
    check(svunitname1_f(), "$unit.svunitname1_f");
    check(c.m(), "$unit.svunitname1_c.m");
    svunitname1_t(s);
    check(s, "$unit.svunitname1_t.blk");
    if (errs == 0) $display("PASSED");
  end
endmodule
