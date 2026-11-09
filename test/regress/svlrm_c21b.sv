`timescale 1ns/1ns
module svlrm_c21b;
  // Self-checking test. Expected values: IEEE 1800-2017 21.4 ($readmemh/$readmemb file format: // comments,
  // @address, white space and _ separators, x/z digits; a word whose leftmost digit is x or z is
  // extended with x/z, 5.7.1; locations not written keep their old value; start/finish arguments),
  // 21.4.2 ($writememh/$writememb are checked by reading the written file back into another array,
  // since the file layout is not defined beyond being readable by $readmem), 21.6 ($test$plusargs and
  // $value$plusargs return 0 for an absent plusarg), 21.2.1 ($write/$swrite concatenate arguments
  // without separators; unformatted integers print with %d width 11, $displayb/o/h etc. use
  // binary/octal/hex with full width, %d/%h/%b/%o of x and z: lower case when all bits of the digit
  // are x or z, upper case when mixed, %d pads to the width of the type), 21.2.1.3 (no `0` flag for
  // %d), 5.9 (string escapes), 21.3 ($timeformat: units, precision, suffix, minimum width; default
  // units = precision, width 20). The $display family is checked through $swrite and through
  // $fdisplayb/o/h, $fwriteb/o/h read back from a file ($swriteb/o/h are not used). Not checked: how $readmemh treats the file word @4 outside a start/finish
  // window, zero flag on negative %05d.
  reg [15:0] m [0:5];
  reg [15:0] mr [0:2];
  reg [7:0] b [4];
  reg [7:0] br [4];
  int fd, r;
  string s;
  int v;
  int errs;
  function automatic void ck(bit ok, string msg);
    if (!ok) begin errs++; $error("%s", msg); end
  endfunction
  function automatic string rep(string c, int n);
    string r = "";
    repeat (n) r = {r, c};
    return r;
  endfunction
  initial begin
    fd = $fopen("c21b_h.txt", "w");
    $fdisplay(fd, "// comment");
    $fdisplay(fd, "dead BEEF 0123");
    $fdisplay(fd, "@4 ff");
    $fdisplay(fd, "abcd");
    $fclose(fd);
    foreach (m[i]) m[i] = 16'hxxxx;
    $readmemh("c21b_h.txt", m);
    ck(m[0] === 16'hdead && m[1] === 16'hbeef && m[2] === 16'h0123, "readmemh sequential words");
    ck(m[3] === 16'hxxxx, "location between words keeps old value");
    ck(m[4] === 16'h00ff && m[5] === 16'habcd, "@address then following word");
    fd = $fopen("c21b_b.txt", "w");
    $fdisplay(fd, "1010 0101_1111 x01z");
    $fclose(fd);
    $readmemb("c21b_b.txt", b);
    ck(b[0] === 8'b0000_1010 && b[1] === 8'b0101_1111, "readmemb words with _ separator");
    ck(b[2] === 8'bxxxx_x01z, "readmemb x extension of a word starting with x");
    ck(b[3] === 8'bxxxx_xxxx, "untouched location stays x");
    $readmemh("c21b_h.txt", m, 1, 3);
    ck(m[1] === 16'hdead && m[2] === 16'hbeef && m[3] === 16'h0123, "readmemh with start and finish");
    $writememh("c21b_w.txt", m, 0, 2);
    foreach (mr[i]) mr[i] = 16'h5555;
    $readmemh("c21b_w.txt", mr);
    ck(mr[0] === m[0] && mr[1] === m[1] && mr[2] === m[2], "writememh round trip");
    $writememb("c21b_wb.txt", b);
    foreach (br[i]) br[i] = 8'h55;
    $readmemb("c21b_wb.txt", br);
    ck(br[0] === b[0] && br[1] === b[1] && br[2] === b[2] && br[3] === b[3], "writememb round trip incl. x/z");
    ck($test$plusargs("nothere") == 0, "test plusargs absent");
    ck($value$plusargs("nothere=%d", v) == 0, "value plusargs absent");
    $display("a", "b", 5, "c");   // exercised; the same formatting is checked through $swrite below
    $swrite(s, "a", "b", 5, "c"); ck(s == {"ab", rep(" ", 10), "5c"}, "args concatenate without separator");
    $swrite(s, "w", 1, 2); ck(s == {"w", rep(" ", 10), "1", rep(" ", 10), "2"}, "write of several ints");
    fd = $fopen("c21b_r.txt", "w");
    $fdisplayb(fd, 5); $fdisplayo(fd, 64); $fdisplayh(fd, 255);
    $fwriteb(fd, 5); $fwriteo(fd, 8); $fwriteh(fd, 10); $fdisplay(fd, "");
    $fclose(fd);
    fd = $fopen("c21b_r.txt", "r");
    r = $fgets(s, fd); ck(s == {rep("0", 29), "101\n"}, "displayb default");
    r = $fgets(s, fd); ck(s == "00000000100\n", "displayo default");
    r = $fgets(s, fd); ck(s == "000000ff\n", "displayh default");
    r = $fgets(s, fd); ck(s == {rep("0", 29), "101", "00000000010", "0000000a", "\n"}, "writeb/o/h default radix, no separators");
    $fclose(fd);
    $displayb(5); $displayo(64); $displayh(255);   // exercised on stdout as well
    s = $sformatf("%s", "a\tb\\n\x41\101");
    ck(s.len() == 7 && s.getc(0) == 97 && s.getc(1) == 9 && s.getc(2) == 98 && s.getc(3) == 92 &&
       s.getc(4) == 110 && s.getc(5) == 65 && s.getc(6) == 65, "string escapes");
    s = $sformatf("%d %d", -5, 5); ck(s == {rep(" ", 9), "-5 ", rep(" ", 10), "5"}, "%d signed width 11");
    s = $sformatf("%0d %0d", -5, 5); ck(s == "-5 5", "%0d");
    s = $sformatf("%5d|%-5d|%05d", -5, -5, -5);
    ck(s.len() >= 12 && s.substr(0, 11) == "   -5|-5   |" && s.getc(s.len() - 1) == 53, "%5d %-5d and %05d");
    s = $sformatf("%x %X", 255, 255); ck(s == "000000ff 000000ff", "%x %X");
    s = $sformatf("%b", -1); ck(s == rep("1", 32), "%b of -1");
    s = $sformatf("%h", -1); ck(s == "ffffffff", "%h of -1");
    s = $sformatf("%0h %0b", 8'h0f, 8'h0f); ck(s == "f 1111", "%0h %0b");
    s = $sformatf("%h %h", 12'hxzf, 12'h1x2); ck(s == "xzf 1x2", "%h x/z digits");
    s = $sformatf("%b %d %o", 4'b10x1, 4'b10x1, 6'b10x1zz); ck(s == "10x1  X XZ", "mixed x/z in %b %d %o");
    s = $sformatf("%d %d", 4'bxxxx, 4'bzzzz); ck(s == " x  z", "all x / all z in %d");
    s = $sformatf("%d %d %d", 4'b1x00, 8'hxz, 8'bz); ck(s == " X   X   z", "%d with x and z at several widths");
    s = $sformatf("%e %f", 1.5, 1.5); ck(s == "1.500000e+00 1.500000", "%e %f");
    s = $sformatf("%10.3f|%-10.3f|", 3.14159, 3.14159); ck(s == "     3.142|3.142     |", "%f with widths");
    s = $sformatf("%t", 12345); ck(s == {rep(" ", 15), "12345"}, "%t default width 20");
    $timeformat(-9, 2, " ns", 10);
    s = $sformatf("%t", 5ns); ck(s == "   5.00 ns", "%t after $timeformat");
    s = $sformatf("%t", 12345); ck(s == "12345.00 ns", "%t after $timeformat, wider than minimum");
    if (errs == 0) $display("PASSED");
  end
endmodule
