// Expected values: IEEE 1800-2017 7.3.2 (tagged unions), 11.9 (tagged union
// expressions and member access), 21.2.1.7 (%p assignment-pattern format).
// The exact text layout of %p for tagged unions is not fixed by the LRM, so
// the checks are on the stored values (via 'matches') and on the format
// output only containing the active member name and its value.
module svptagged1;
  typedef union tagged { void invalid; bit [3:0] v; } u_t;
  typedef struct { u_t u; int z; } w_t;
  u_t arr[2]; u_t u; w_t w, ws[2]; u_t q[$];
  int errs;
  task automatic chk(bit c, string m); if (!c) begin $error("%s", m); errs++; end endtask
  function automatic bit has(string s, string sub);
    for (int i = 0; i + sub.len() <= s.len(); i++) if (s.substr(i, i + sub.len() - 1) == sub) return 1;
    return 0;
  endfunction
  initial begin
    string s;
    u = tagged v (1);
    arr[0] = tagged v (1); arr[1] = tagged invalid;
    chk(arr[0] matches tagged v ? 1 : 0, "arr[0] tag");
    chk(arr[1] matches tagged invalid ? 1 : 0, "arr[1] tag");
    chk(arr[0].v == 4'd1, "arr[0].v");
    w.u = tagged v (2); w.z = 3;
    ws[0] = w; ws[1].u = tagged invalid;
    chk(ws[0].u matches tagged v ? 1 : 0, "ws[0].u tag");
    chk(ws[0].u.v == 2, "ws[0].u.v");
    chk(ws[0].z == 3, "ws[0].z");
    chk(ws[1].u matches tagged invalid ? 1 : 0, "ws[1].u");
    q.push_back(u); begin u_t t; t = tagged invalid; q.push_back(t); end
    chk(q.size() == 2, "q size");
    chk(q[0] matches tagged v ? 1 : 0, "q[0] tag");
    chk(q[0].v == 1, "q[0].v");
    chk(q[1] matches tagged invalid ? 1 : 0, "q[1]");
    // %p must render each tagged element; both flavours of the specifier agree
    // on the information content (active member name / value).
    s = $sformatf("%p", arr);  chk(has(s, "invalid") && has(s, "1"), {"%p arr: ", s});
    s = $sformatf("%0p", arr); chk(has(s, "invalid") && has(s, "1"), {"%0p arr: ", s});
    s = $sformatf("%p", ws);   chk(has(s, "invalid") && has(s, "2") && has(s, "3"), {"%p ws: ", s});
    s = $sformatf("%p", q);    chk(has(s, "invalid") && has(s, "1"), {"%p q: ", s});
    s = $sformatf("%p", u);    chk(s.len() > 0 && has(s, "1") && !has(s, "invalid"), {"%p u: ", s});
    s = $sformatf("%0p", w);   chk(has(s, "2") && has(s, "3"), {"%0p w: ", s});
    if (errs == 0) $display("PASSED");
  end
endmodule
