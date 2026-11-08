// %p of a chandle prints null or the value of the handle
// IEEE 1800-2017 21.2.1.7: %p prints a singular expression as an element of an aggregate;
// 6.14: a chandle is null or holds a value from foreign code.  nvc prints a chandle like a
// class handle: null, or the value of the pointer as a number.
class svchandlefmt1_c;
  chandle m;
endclass
module svchandlefmt1;
  import "DPI-C" function chandle dpi_new(input int n);
  import "DPI-C" function void dpi_free(input chandle h);
  chandle h;
  int errs = 0;
  function automatic bit is_number(string s);
    if (s.len() == 0) return 0;
    foreach (s[i]) if (s[i] < "0" || s[i] > "9") return 0;
    return 1;
  endfunction
  task automatic local_null();
    chandle l;
    if ($sformatf("%p", l) != "null") begin errs++; $display("local: %p", l); end
  endtask
  initial begin
    automatic svchandlefmt1_c c = new;
    string s;
    if ($sformatf("%p", h) != "null") begin errs++; $display("null: %p", h); end
    if ($sformatf("%p", c.m) != "null") begin errs++; $display("member: %p", c.m); end
    local_null();
    h = dpi_new(3);
    c.m = h;
    s = $sformatf("%p", h);
    if (!is_number(s)) begin errs++; $display("non-null: %s", s); end
    if ($sformatf("%p", c.m) != s) begin errs++; $display("member: %p", c.m); end
    dpi_free(h);
    if (errs == 0) $display("PASSED");
  end
endmodule
