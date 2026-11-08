// $writememh/$writememb write one value per line, in address order, using the
// element width (4 hex digits / 8 binary digits); x and z digits are kept as such.
// Expected values: IEEE 1800-2017 21.4.1, 21.4.2, 21.4.3, 21.3.4.2
// (hex-letter case and any address/comment lines are not checked)
module svwritemem1;
  reg [15:0] m [0:3];
  reg [7:0] b [0:1];
  reg [15:0] m2 [0:3];
  reg [7:0] b2 [0:1];
  int fd; int n; int errs; string s;
  string hl [$];
  string bl [$];

  function automatic string lower(string x);
    return x.tolower();
  endfunction

  task automatic slurp(string fn, ref string q[$]);
    int f; string l; int len;
    f = $fopen(fn, "r");
    if (f == 0) begin errs++; $error("cannot open %s", fn); return; end
    while ($fgets(l, f) > 0) begin
      len = l.len();
      while (len > 0 && (l[len-1] == "\n" || l[len-1] == "\r")) len--;
      l = l.substr(0, len - 1);
      if (len == 0 || l[0] == "@" || l[0] == "/") continue;
      q.push_back(lower(l));
    end
    $fclose(f);
  endtask

  initial begin
    errs = 0;
    m[0] = 16'hdead; m[1] = 16'hbeef; m[2] = 16'h0123; m[3] = 16'hxx0z;
    b[0] = 8'hab; b[1] = 8'h1x;
    $writememh("/tmp/svwritemem1_h.txt", m);
    $writememb("/tmp/svwritemem1_b.txt", b);
    slurp("/tmp/svwritemem1_h.txt", hl);
    slurp("/tmp/svwritemem1_b.txt", bl);
    if (hl.size() != 4) begin errs++; $error("hex line count %0d", hl.size()); end
    else begin
      if (hl[0] != "dead") begin errs++; $error("hex[0] '%s'", hl[0]); end
      if (hl[1] != "beef") begin errs++; $error("hex[1] '%s'", hl[1]); end
      if (hl[2] != "0123") begin errs++; $error("hex[2] '%s'", hl[2]); end
      if (hl[3] != "xx0z") begin errs++; $error("hex[3] '%s'", hl[3]); end
    end
    if (bl.size() != 2) begin errs++; $error("bin line count %0d", bl.size()); end
    else begin
      if (bl[0] != "10101011") begin errs++; $error("bin[0] '%s'", bl[0]); end
      if (bl[1] != "0001xxxx") begin errs++; $error("bin[1] '%s'", bl[1]); end
    end
    // round trip through $readmemh/$readmemb (21.4.1)
    foreach (m2[i]) m2[i] = 16'h5a5a;
    foreach (b2[i]) b2[i] = 8'h5a;
    $readmemh("/tmp/svwritemem1_h.txt", m2);
    $readmemb("/tmp/svwritemem1_b.txt", b2);
    foreach (m[i]) if (m2[i] !== m[i]) begin errs++; $error("readmemh m2[%0d]=%b", i, m2[i]); end
    foreach (b[i]) if (b2[i] !== b[i]) begin errs++; $error("readmemb b2[%0d]=%b", i, b2[i]); end
    if (errs == 0) $display("PASSED");
  end
endmodule
