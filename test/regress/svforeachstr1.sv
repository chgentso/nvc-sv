// foreach over a string variable and a string argument
// Expected values: IEEE 1800-2017 12.7.3 (foreach over a string visits indices 0..len-1; an empty string has
// no iterations), 6.16 (string length), 13.5 (arguments).
module svforeachstr1;
  string s = "str"; string e = "";
  int errs = 0;
  int fn_n, fn_len;
  function void f(string value);
    automatic int n = 0;
    foreach (value[i]) n++;
    fn_n = n; fn_len = value.len();
  endfunction
  task automatic chk(bit ok, string m);
    if (!ok) begin $error("%s", m); errs++; end
  endtask
  initial begin
    automatic int n = 0;
    automatic string idx = "";
    foreach (s[i]) n++;
    $display("n=%0d len=%0d", n, s.len());
    chk(n == 3 && s.len() == 3, "foreach over s must run 3 times");
    n = 0; foreach (e[i]) n++;
    $display("empty n=%0d", n);
    chk(n == 0, "foreach over an empty string must not run");
    f("str");
    chk(fn_n == 3 && fn_len == 3, "f(\"str\")");
    f(s);
    chk(fn_n == 3 && fn_len == 3, "f(s)");
    foreach (s[i]) idx = {idx, $sformatf("%0d ", i)};
    $display("%s", idx);
    chk(idx == "0 1 2 ", "indices must be 0 1 2");
    if (errs == 0) $display("PASSED");
  end
endmodule
