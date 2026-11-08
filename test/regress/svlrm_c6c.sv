module svlrm_c6c;
  // Self-checking test. Expected values: IEEE 1800-2017 6.16 (string data type and its methods
  // len, putc, getc, toupper, tolower, compare, icompare, substr, atoi/atohex/atooct/atobin/atoreal,
  // itoa/hextoa/octtoa/bintoa/realtoa), 11.4.5 (string comparison), 11.4.12 (concatenation,
  // replication), 21.2.1 (%s with field width), 6.17 and 15.5 (events, ->, @).
  // The sign of compare() is checked, never its magnitude.
  string s = "hello", t;
  string e = "";
  event ev1, ev2;
  int errs;
  bit got1, got2, pastwait;
  time t1, t2;
  function automatic void ck(bit ok, string m);
    if (!ok) begin errs++; $error("%s", m); end
  endfunction
  initial begin
    ck(s.len() == 5, "len");
    t = {s, " ", "world"}; ck(t == "hello world" && t.len() == 11, "concat");
    ck(s.toupper() == "HELLO" && t.tolower() == "hello world", "toupper/tolower");
    ck(s.substr(1, 3) == "ell", "substr");
    ck(s.compare("hello") == 0, "compare equal");
    ck(s.compare("help") < 0, "compare less");
    ck(s.icompare("HELLO") == 0, "icompare");
    ck(s.compare("a") > 0, "compare greater");
    s.putc(0, "J"); ck(s == "Jello" && s.getc(1) == 101, "putc/getc");
    ck(("abc" < "abd") == 1, "abc < abd");
    ck((s == "Jello") == 1, "==");
    ck((s != "x") == 1 && (s > "A") == 1, "!= and >");
    ck(e == "" && e.len() == 0, "empty string");
    s = "12345"; ck(s.atoi() == 12345 && s.atoi() + 1 == 12346, "atoi");
    s = "ff";   ck(s.atohex() == 255, "atohex");
    s = "777";  ck(s.atooct() == 511, "atooct");
    s = "1010"; ck(s.atobin() == 10, "atobin");
    s = "3.75"; ck(s.atoreal() == 3.75, "atoreal");
    s = "abc"; s = {s, s}; ck(s == "abcabc", "concat self");
    s = {3{"xy"}}; ck(s == "xyxyxy", "replication");
    s = "A"; ck(s[0] == 65, "index");
    ck(string'(65) == "A", "string cast of int");
    t.itoa(42);     ck(t == "42", "itoa");
    t.hextoa(255);  ck(t == "ff", "hextoa");
    t.bintoa(5);    ck(t == "101", "bintoa");
    t.octtoa(64);   ck(t == "100", "octtoa");
    t.realtoa(1.5); ck(t == "1.5", "realtoa");
    ck($sformatf("%0d-%s-%h", 5, "x", 8'hab) == "5-x-ab", "sformatf");
    s = "abc"; ck((s.len() == 3) == 1, "len of abc");
    ck($sformatf("[%10s] [%-10s]", s, s) == "[       abc] [abc       ]", "%s width");
    // events
    fork
      begin @ev1; got1 = 1; t1 = $time; ->ev2; end
      begin #5; ->ev1; end
      begin @ev2; got2 = 1; t2 = $time; end
    join
    ck(got1 && got2 && t1 == 5 && t2 == 5, "event chain at time 5");
    -> ev1; @ev1; pastwait = 1;   // nothing triggers ev1 again: this process stays blocked
  end
  initial begin
    #20;
    ck(!pastwait, "@ev1 after -> ev1 must block (15.5.1)");
    if (errs == 0) $display("PASSED");
  end
endmodule
