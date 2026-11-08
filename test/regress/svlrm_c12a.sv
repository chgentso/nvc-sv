module svlrm_c12a;
  // Self-checking test. Expected values: IEEE 1800-2017 12.4 (if / unique if / priority if; a
  // condition with an unknown value takes the else branch, a vector with a known 1 bit is true),
  // 12.5 (case, casez, casex, case inside; case compares with ===, z is a wildcard in casez, x and z
  // in casex; first matching item wins; unique/priority case), 12.7 (for, while, do-while, repeat,
  // forever, foreach incl. multi-dimensional), 12.8 (break, continue).
  int i, j, n;
  logic [3:0] v;
  typedef enum {A, B, C, D} e_t;
  e_t e;
  int errs;
  string o, r;
  function automatic void ck(bit ok, string m);
    if (!ok) begin errs++; $error("%s", m); end
  endfunction
  function automatic void ap(string s); o = {o, s}; endfunction
  initial begin
    o = ""; for (i = 0; i < 3; i++) ap($sformatf("%0d ", i)); ck(o == "0 1 2 ", "for");
    o = ""; for (int k = 10; k > 0; k -= 4) ap($sformatf("%0d ", k)); ck(o == "10 6 2 ", "for step");
    o = ""; for (i = 0, j = 10; i < j; i += 3, j -= 3) ap($sformatf("%0d,%0d ", i, j)); ck(o == "0,10 3,7 ", "for two vars");
    o = ""; i = 0; while (i < 3) begin ap($sformatf("w%0d ", i)); i++; end ck(o == "w0 w1 w2 ", "while");
    o = ""; i = 5; do begin ap($sformatf("d%0d ", i)); i--; end while (i > 3); ck(o == "d5 d4 ", "do while");
    i = 0; repeat (4) i += 2; ck(i == 8, "repeat");
    i = 0; forever begin i++; if (i == 5) break; end ck(i == 5, "forever/break");
    o = ""; for (i = 0; i < 6; i++) begin if (i % 2) continue; ap($sformatf("%0d ", i)); end ck(o == "0 2 4 ", "continue");
    o = ""; for (i = 0; i < 3; i++) for (j = 0; j < 3; j++) begin if (j == 2) break; ap($sformatf("%0d%0d ", i, j)); end
    ck(o == "00 01 10 11 20 21 ", "nested break");
    begin int arr[4]; arr = '{1,2,3,4}; o = ""; foreach (arr[i]) ap($sformatf("%0d ", arr[i])); ck(o == "1 2 3 4 ", "foreach"); end
    begin int m[2][3]; foreach (m[i,j]) m[i][j] = i * 10 + j; o = ""; foreach (m[i,j]) ap($sformatf("%0d ", m[i][j]));
      ck(o == "0 1 2 10 11 12 ", "foreach 2-d"); end
    begin int m[2][3]; foreach (m[i,j]) m[i][j] = i + j; ck(m[1][2] == 3, "foreach 2-d sum"); end
    v = 4'b1010;
    case (v) 4'b1010: r = "case match"; default: r = "case default"; endcase ck(r == "case match", "case");
    r = "none"; case (v) 4'b10x0: r = "x match"; 4'b1010, 4'b0000: r = "list match"; endcase ck(r == "list match", "case x item never matches");
    r = "none"; casez (v) 4'b1??0: r = "casez match"; endcase ck(r == "casez match", "casez ?");
    casez (4'b1z10) 4'b1010: r = "casez z"; default: r = "casez none"; endcase ck(r == "casez z", "casez z in expression");
    casex (4'b1x10) 4'b1010: r = "casex x"; default: r = "casex none"; endcase ck(r == "casex x", "casex x in expression");
    r = "none"; case (1'b1) v[0]: r = "bit0"; v[1]: r = "bit1"; endcase ck(r == "bit1", "case with bit-select items");
    e = C;
    case (e) A: r = "A"; B, C: r = "B or C"; default: r = "D"; endcase ck(r == "B or C", "enum case");
    r = "none"; unique case (e) A: r = "uA"; C: r = "uC"; D: r = "uD"; endcase ck(r == "uC", "unique case");
    r = "none"; priority case (e) A: r = "pA"; B, C: r = "pBC"; endcase ck(r == "pBC", "priority case");
    r = "none"; unique if (v == 0) r = "u0"; else if (v == 10) r = "u10"; ck(r == "u10", "unique if");
    priority if (v == 10) r = "p10"; else r = "pother"; ck(r == "p10", "priority if");
    if (v == 4'ha) r = "if"; else r = "else"; ck(r == "if", "if");
    r = "none"; if (v === 4'bx) r = "x"; else if (v === 4'ha) r = "a"; ck(r == "a", "if === chain");
    if (4'bx) r = "bad"; else r = "x false"; ck(r == "x false", "x condition is false");
    r = "none"; if (4'b0x1) r = "0x1 true"; ck(r == "0x1 true", "vector with known 1 is true");
    if (4'b0x0) r = "bad2"; else r = "0x0 false"; ck(r == "0x0 false", "0x0 is unknown, hence false");
    case (4'bx) 1'bx: r = "x case"; default: r = "x default"; endcase ck(r == "x default", "case uses ===, item 000x != xxxx");
    case (3'd5) inside [0:2]: r = "low"; [3:6]: r = "mid"; default: r = "hi"; endcase ck(r == "mid", "case inside range");
    i = 5; case (i) inside 1, 2, [4:6]: r = "in set"; default: r = "out"; endcase ck(r == "in set", "case inside set");
    begin string s; s = "abc"; r = "none"; case (s) "xyz": r = "xyz"; "abc": r = "str match"; endcase ck(r == "str match", "string case"); end
    if (errs == 0) $display("PASSED");
  end
endmodule
