// String utilities: split, trim, join, number conversion and a tiny expression evaluator
// Expected values: IEEE 1800-2017 6.16 (string methods: len, substr, toupper, tolower, compare
// (only the sign is defined), icompare, putc, getc, atoi, atohex, atobin, atoreal, itoa; string
// comparison operators and concatenation), 21.2.1 (a field width in %5s, %-5s, %05d is a minimum
// width: a decimal value is filled with zeros after a leading 0 in the width, %x of a 32-bit value has 8 digits), 21.3.3
// ($sscanf, %t with the default time format in a 1 ns time unit), 11.4.12.1 (replication).
// All values are derived by hand.
`timescale 1ns/1ns
package strutil;
  function automatic string trim(string s);
    int b = 0, e = s.len() - 1;
    while (b <= e && (s[b] == " " || s[b] == "\t")) b++;
    while (e >= b && (s[e] == " " || s[e] == "\t")) e--;
    return s.substr(b, e);
  endfunction
  function automatic void split(string s, byte sep, ref string parts[$]);
    int start = 0;
    parts.delete();
    for (int i = 0; i <= s.len(); i++) begin
      if (i == s.len() || s[i] == sep) begin
        parts.push_back(trim(s.substr(start, i - 1)));
        start = i + 1;
      end
    end
  endfunction
  function automatic string str_join(string parts[$], string sep);
    string r;
    foreach (parts[i]) r = (i == 0) ? parts[i] : {r, sep, parts[i]};
    return r;
  endfunction
  function automatic int find(string hay, string needle, int from = 0);
    for (int i = from; i + needle.len() <= hay.len(); i++)
      if (hay.substr(i, i + needle.len() - 1) == needle) return i;
    return -1;
  endfunction
  function automatic string replace_all(string s, string from, string to);
    string r;
    int p, pos;
    while ((pos = find(s, from, p)) >= 0) begin
      r = {r, s.substr(p, pos - 1), to};
      p = pos + from.len();
    end
    return {r, s.substr(p, s.len() - 1)};
  endfunction
  function automatic string to_hex(int unsigned v, int width = 0);
    string r;
    string digits = "0123456789abcdef";
    if (v == 0) r = "0";
    while (v != 0) begin
      r = {string'(digits[v & 15]), r};
      v >>= 4;
    end
    while (r.len() < width) r = {"0", r};
    return r;
  endfunction
  function automatic int eval_rpn(string expr);
    int stack[$];
    string tok[$];
    split(expr, " ", tok);
    foreach (tok[i]) begin
      case (tok[i])
        "+": begin int b = stack.pop_back(), a = stack.pop_back(); stack.push_back(a + b); end
        "-": begin int b = stack.pop_back(), a = stack.pop_back(); stack.push_back(a - b); end
        "*": begin int b = stack.pop_back(), a = stack.pop_back(); stack.push_back(a * b); end
        "/": begin int b = stack.pop_back(), a = stack.pop_back(); stack.push_back(b == 0 ? 0 : a / b); end
        default: stack.push_back(tok[i].atoi());
      endcase
    end
    return stack.size() ? stack[$] : 0;
  endfunction
endpackage

module svsyn11;
  import strutil::*;
  string parts[$], words[$];
  string line = "  alpha , beta,gamma ,  delta  ";
  string csv;
  int unsigned hv;
  real rv;
  string s2, h1, h2, h3, h4;
  int errs;
  task automatic cks(string what, string got, string exp);
    if (got != exp) begin $error("%s: got '%s' expected '%s'", what, got, exp); errs++; end
  endtask
  task automatic cki(string what, int got, int exp);
    if (got !== exp) begin $error("%s: got %0d expected %0d", what, got, exp); errs++; end
  endtask
  initial begin
    automatic string exp_p[$] = '{"alpha", "beta", "gamma", "delta"};
    automatic string exp_u[$] = '{"ALPHA", "BETA", "GAMMA", "DELTA"};
    automatic string exp_sorted[$] = '{"alpha", "beta", "delta", "gamma"};
    automatic string exp_rev[$] = '{"gamma", "delta", "beta", "alpha"};
    split(line, ",", parts);
    cki("n", parts.size(), 4);
    cks("join", str_join(parts, "|"), "alpha|beta|gamma|delta");
    foreach (parts[i]) begin
      cks("part", parts[i], exp_p[i]);
      cki("len", parts[i].len(), exp_p[i].len());
      cks("upper", parts[i].toupper(), exp_u[i]);
    end
    words = parts;
    for (int i = 0; i < words.size(); i++)
      for (int j = i + 1; j < words.size(); j++)
        if (words[j] < words[i]) begin automatic string t = words[i]; words[i] = words[j]; words[j] = t; end
    foreach (words[i]) cks("sorted", words[i], exp_sorted[i]);
    words.reverse();
    foreach (words[i]) cks("reversed", words[i], exp_rev[i]);
    cki("find o", find("hello world", "o"), 4);
    cki("find o from 5", find("hello world", "o", 5), 7);
    cki("find xyz", find("hello", "xyz"), -1);
    cks("replace", replace_all("a-b-c-d", "-", "::"), "a::b::c::d");
    cks("replace2", replace_all("xxxx", "xx", "y"), "yy");
    cks("hex 255", to_hex(255), "ff");
    cks("hex 4096", to_hex(4096, 6), "001000");
    cks("hex 0", to_hex(0), "0");
    cki("rpn1", eval_rpn("3 4 + 2 *"), 14);
    cki("rpn2", eval_rpn("100 5 / 3 -"), 17);
    cki("rpn3", eval_rpn("7 0 /"), 0);
    s2 = "Hello";
    s2 = {s2, ", ", "World", {3{"!"}}};
    cks("concat", s2, "Hello, World!!!");
    cki("len", s2.len(), 15);
    if (!(s2.compare("Hello") > 0)) begin $error("compare sign"); errs++; end   // only the sign is defined
    cki("icompare", s2.icompare("HELLO, WORLD!!!"), 0);
    s2.putc(0, "J");
    cks("putc", s2, "Jello, World!!!");
    cks("tolower", s2.tolower(), "jello, world!!!");
    cki("getc", s2.getc(1), "e");
    h1 = "ff"; h2 = "3.25"; h3 = "-42x"; h4 = "1011";
    hv = h1.atohex(); rv = h2.atoreal();
    cki("atohex", hv, 255);
    if (!(rv == 3.25)) begin $error("atoreal %f", rv); errs++; end
    cki("atoi", h3.atoi(), -42);
    cki("atobin", h4.atobin(), 11);
    csv = "";
    for (int i = 0; i < 5; i++) begin
      string cel;
      cel.itoa(i * i);
      csv = {csv, i == 0 ? "" : ";", cel};
    end
    cks("csv", csv, "0;1;4;9;16");
    cks("sformatf", $sformatf("%5s|%-5s|%05d|%x|%0t", "ab", "cd", 42, 255, 10ns), "   ab|cd   |00042|000000ff|10");
    begin
      int a, b; string name;
      automatic int n = $sscanf("x=12 y=34 tag", "x=%d y=%d %s", a, b, name);
      cki("sscanf n", n, 3); cki("sscanf a", a, 12); cki("sscanf b", b, 34); cks("sscanf name", name, "tag");
    end
    cks("trim", {"[", trim("   x y  "), "]"}, "[x y]");
    cki("lt", "abc" < "abd", 1); cki("eq", "abc" == "abc", 1);
    if (errs == 0) $display("PASSED");
  end
endmodule
