`timescale 1ns/1ns
module svlrm_c21a;
  // Self-checking test. Expected values: IEEE 1800-2017 21.2.1 (display format specifiers; %d pads
  // to the width of the type unless a field width or 0 is given, %h/%b pad with zeros to the full
  // width, %s of a vector prints its bytes, %m prints the scope name, real formats follow C printf),
  // 21.2.1.3 (`%05d`, `%03d` fill a decimal with zeros as C does; explicit widths on %b are minimum
  // widths), 21.3.1 (a $fopen with a mode returns a descriptor with bit 31 set), 21.3.2 ($fflush,
  // $fclose), 21.3.4 ($fgets returns the number of characters read, $fgetc, $ungetc, $ftell, $fseek,
  // $rewind), 21.3.4.3 ($fscanf/$sscanf: %d %s %f %h %b %o %c, white space skipping, sign, `_` in
  // numbers, x digits in %h become 0 in a 2-state target), 21.3.3 ($sformat, $swrite, $sformatf),
  // 21.2.1.? (%t uses $timeformat defaults: units = precision, 0 digits, width 20).
  // Not checked (implementation-defined or undefined in the LRM): width given to %c, `%5%`, zero
  // flag on %f (`%08.3f`: only the digits are compared), descriptor values beyond bit 31.
  int fd, n, c, r;
  string line, s;
  int a, b; real x; bit [15:0] h; string w1, w2;
  reg [7:0] mem [0:7];
  string lines[$];
  int errs;
  function automatic void ck(bit ok, string m);
    if (!ok) begin errs++; $error("%s", m); end
  endfunction
  // drop leading spaces and zeros
  function automatic string lstrip(string t);
    int i = 0;
    while (i < t.len() && (t[i] == " " || t[i] == "0")) i++;
    return t.substr(i, t.len() - 1);
  endfunction
  function automatic string rep(string c, int n);
    string r = "";
    repeat (n) r = {r, c};
    return r;
  endfunction
  function automatic bit starts(string t, string p);
    return t.len() >= p.len() && t.substr(0, p.len() - 1) == p;
  endfunction
  function automatic bit ends(string t, string p);
    return t.len() >= p.len() && t.substr(t.len() - p.len(), t.len() - 1) == p;
  endfunction
  initial begin
    fd = $fopen("c21a_out.txt", "w");
    ck(fd != 0 && fd[31] == 1'b1, "fopen with mode returns a descriptor (bit 31 set)");
    $fdisplay(fd, "hello %0d", 42);
    $fwrite(fd, "no newline ");
    $fwrite(fd, "%s|%5d|%-5d|%05d\n", "str", 12, 34, 56);
    $fdisplay(fd, "%h %b %o %c", 8'hab, 4'b1010, 9'o777, 8'd65);
    $fdisplay(fd, "%0t %f %e %g", 10, 3.14159, 1234.5, 0.0001);

    a = 5; b = 7; #1 b = 8; #1;
    $fflush(fd);
    $fclose(fd);
    fd = $fopen("c21a_out.txt", "r");
    while (!$feof(fd)) begin
      r = $fgets(line, fd);
      if (r > 0) begin
        ck(r == line.len(), "fgets returns the number of characters read");
        lines.push_back(line);
      end
    end
    $fclose(fd);
    ck(lines.size() == 4, "four lines in the file");
    ck(lines[0] == "hello 42\n", "line 1");
    ck(lines[1] == "no newline str|   12|34   |00056\n", "line 2");
    ck(lines[2] == "ab 1010 777 A\n", "line 3");
    ck(lines[3] == "10 3.141590 1.234500e+03 0.0001\n", "line 4");
    fd = $fopen("c21a_out.txt", "r");
    c = $fgetc(fd); ck(c == "h", "fgetc");
    r = $ungetc(c, fd); c = $fgetc(fd); ck(c == "h", "ungetc then fgetc");
    r = $ftell(fd); ck(r == 1, "ftell after one character");
    r = $fseek(fd, 0, 0); c = $fgetc(fd); ck(c == "h", "fseek to start");
    r = $fseek(fd, -3, 2); r = $fgets(line, fd); ck(line == "01\n", "fseek from end");
    $rewind(fd); r = $fscanf(fd, "%s %d", w1, a); ck(r == 2 && w1 == "hello" && a == 42, "fscanf %s %d");
    r = $fscanf(fd, "%s %s", w1, w2); ck(r == 2 && w1 == "no" && w2 == "newline", "fscanf %s %s");
    $fclose(fd);
    r = $sscanf("12 34 abc 1.5 ff", "%d %d %s %f %h", a, b, w1, x, h);
    ck(r == 5 && a == 12 && b == 34 && w1 == "abc" && x == 1.5 && h === 16'h00ff, "sscanf mixed");
    r = $sscanf("x=7,y=9", "x=%d,y=%d", a, b); ck(r == 2 && a == 7 && b == 9, "sscanf literal text");
    r = $sscanf("abc", "%d", a); ck(r == 0, "sscanf no match");
    r = $sscanf("0x1f", "%h", a); ck(r == 1 && a == 31, "sscanf %h with x digit into 2-state int");
    r = $sscanf("101", "%b", a); ck(r == 1 && a == 5, "sscanf %b");
    r = $sscanf("777", "%o", a); ck(r == 1 && a == 511, "sscanf %o");
    r = $sscanf("A", "%c", a); ck(r == 1 && a == 65, "sscanf %c");
    r = $sscanf("  12  ", "%d", a); ck(r == 1 && a == 12, "sscanf skips white space");
    r = $sscanf("-5 +6", "%d %d", a, b); ck(r == 2 && a == -5 && b == 6, "sscanf signs");
    r = $sscanf("1_000", "%d", a); ck(r == 1 && a == 1000, "sscanf underscore");
    $sformat(s, "%0d-%s", 5, "x"); ck(s == "5-x", "sformat");
    $swrite(s, "a", "b", 5); ck(s == {"ab", rep(" ", 10), "5"}, "swrite, int printed with %d (width 11)");
    s = $sformatf("%08.3f|%d|%x", 3.14159, 5, 255);
    ck(lstrip(s) == "3.142|          5|000000ff" || lstrip(s) == ".142|          5|000000ff", "sformatf f/d/x");
    s = $sformatf("%5s|%-5s|%5c", "ab", "cd", "e");
    ck(starts(s, "   ab|cd   |") && ends(s, "e"), "sformatf %5s %-5s, %c contains the character");
    s = $sformatf("%10.2e", 1234.5678); ck(s == "  1.23e+03", "%10.2e");
    s = $sformatf("%0d %0h %0b %0o", 255, 255, 5, 64); ck(s == "255 ff 101 100", "%0d %0h %0b %0o");
    s = $sformatf("%%|%5%|"); ck(starts(s, "%|"), "%% prints a percent sign");
    s = $sformatf("%t", 5ns); ck(s == {rep(" ", 19), "5"}, "%t default: 5 units in width 20");
    s = $sformatf("%d|%3d|%-3d|%03d", 5, 5, 5, 5); ck(s == {rep(" ", 10), "5|  5|5  |005"}, "%d widths");
    s = $sformatf("%h|%H|%X", 8'hab, 8'hab, 8'hab); ck(s == "ab|ab|ab", "%h in any case");
    s = $sformatf("%b|%0b|%8b", 5, 5, 5); ck(s == {rep("0", 29), "101|101|00000101"}, "%b widths");
    s = $sformatf("%s", 32'h41424344); ck(s == "ABCD", "%s of vector");
    s = $sformatf("%c%c%c", 72, 105, 33); ck(s == "Hi!", "%c");
    s = $sformatf("%m"); ck(s == "svlrm_c21a", "%m");
    s = $sformatf("%f %F", 1.0, 2.0); ck(s == "1.000000 2.000000", "%f %F");
    s = $sformatf("%g %G", 100000.0, 1e-10); ck(s == "100000 1E-10", "%g %G");
    s = $sformatf("%3.1f|%.0f|%10.4f", 2.55, 2.5, 3.14159265); ck(s == "2.5|2|    3.1416", "C rounding of %f");
    if (errs == 0) $display("PASSED");
  end
endmodule
