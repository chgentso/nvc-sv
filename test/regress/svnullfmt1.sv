// Handle comparisons with null and formatting of handles
// Expected values: IEEE 1800-2017 6.16 (string len), 8.12 / 8.4 (handle == null, != null),
// 21.3.3 ($sformatf with %s)
// The printed values of a handle / null under %d and %h are not specified and are not checked.
class c; int x; endclass
module svnullfmt1;
  c h, n;
  string s;
  int errs = 0;
  initial begin
    h = new;
    s = $sformatf("Check failed %s != %s (%0d [0x%0h] vs %0d [0x%0h])", "h", "null", h, h, null, null);
    if (!(s.len() > 0)) begin $error("sformatf with handle args gave an empty string"); errs++; end
    if (s.substr(0, 20) != "Check failed h != nul") begin $error("sformatf prefix wrong: %s", s); errs++; end
    s = $sformatf("%s != %s (%0d)", "a", "null", h == null);
    if (s != "a != null (0)") begin $error("got '%s'", s); errs++; end
    if ((h != null) !== 1'b1) begin $error("h != null"); errs++; end
    if ((n == null) !== 1'b1) begin $error("n == null"); errs++; end
    if (((h) != (null)) !== 1'b1) begin $error("(h) != (null)"); errs++; end
    if (((n) != (null)) !== 1'b0) begin $error("(n) != (null)"); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
