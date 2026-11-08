// $typename lists packed and unpacked dimensions in declaration order
// IEEE 1800-2017 20.6.1 (type name function: array ranges are shown as
// unsized decimal numbers, an unpacked array of an anonymous type is shown
// with $ in place of its name)
module svtypenamedims1;
  bit [3:0][7:0] pv;
  int a[3:1][0:4];
  logic [1:0] b [2][3];
  int errs;
  function automatic string nows(string s);
    string r = "";
    foreach (s[i]) if (s[i] != " ") r = {r, string'(s[i])};
    return r;
  endfunction
  initial begin
    if (nows($typename(pv)) != "bit[3:0][7:0]") begin errs++; $display("FAILED %s", $typename(pv)); end
    if (nows($typename(a)) != "int$[3:1][0:4]") begin errs++; $display("FAILED %s", $typename(a)); end
    if (nows($typename(b)) != "logic[1:0]$[0:1][0:2]") begin errs++; $display("FAILED %s", $typename(b)); end
    if (errs == 0) $display("PASSED");
  end
endmodule
