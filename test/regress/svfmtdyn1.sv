// Format strings that are not literals (variables, concatenations) in $sformatf, $display, $write, $fdisplay, $swrite
// Expected values: IEEE 1800-2017 21.2.1 (the format argument is any string expression, 21.2.1: "a string
// literal or an expression of string type" / integral holding characters), 21.2.1.8 (%0s), 21.2.1.2 (%0d),
// 21.3.2 ($fdisplay), 21.2.3 ($sformatf, $swrite), 11.4.12 (string concatenation), 20.4.2 ($timeformat, %t).
// Output of $display/$write goes to the log and cannot be read back, so the same format expressions are also written to
// a file with $fdisplay/$fwrite and read back with $fgets.
module svfmtdyn1;
  string f, fl, s, plain, line;
  int fd, errs = 0;
  task automatic eq(string got, string exp, string m);
    if (got != exp) begin $error("%s: got '%s' expected '%s'", m, got, exp); errs++; end
  endtask
  initial begin
    $timeformat(-9, 0, "", 1);
    f = "%0s-%0d";
    s = $sformatf(f, "a", 3);
    eq(s, "a-3", "variable format in $sformatf");
    $display("%s", s);
    plain = "<>";
    fl = "(file:1) ";
    s = $sformatf({"%0s @ %t: ", fl, "obj ", "[%0s] %0s"}, "SEV", $realtime, "id", "msg");
    eq(s, "SEV @ 0: (file:1) obj [id] msg", "concatenated format with %t");
    $display("%s", s);
    s = $sformatf({"x", plain, "%0d"}, 4);
    eq(s, "x<>4", "concatenation with variable");
    $display("%s", s);
    $display({"%0s|", plain, "%0d"}, "c", 5);
    $write({"%0d", plain, "%0s\n"}, 1, "d");
    $fdisplay(32'h80000001, {"%0s ", plain, "%0d"}, "e", 7);
    s = "";
    $swrite(s, {"%0s ", plain, "%0d"}, "g", 8);
    eq(s, "g <>8", "$swrite with concatenated format");
    $display("%s", s);
    $display(f, "h", 9);
    eq($sformatf(f, "i", 10), "i-10", "variable format, second call");
    $display("%s", $sformatf(f, "i", 10));
    // read back the same statements through a file
    fd = $fopen("svfmtdyn1_tmp.txt", "w");
    $fdisplay(fd, {"%0s|", plain, "%0d"}, "c", 5);
    $fwrite(fd, {"%0d", plain, "%0s\n"}, 1, "d");
    $fdisplay(fd, {"%0s ", plain, "%0d"}, "e", 7);
    $fdisplay(fd, f, "h", 9);
    $fclose(fd);
    fd = $fopen("svfmtdyn1_tmp.txt", "r");
    void'($fgets(line, fd)); eq(line, "c|<>5\n", "$fdisplay concatenated format");
    void'($fgets(line, fd)); eq(line, "1<>d\n", "$fwrite concatenated format");
    void'($fgets(line, fd)); eq(line, "e <>7\n", "$fdisplay concatenated format 2");
    void'($fgets(line, fd)); eq(line, "h-9\n", "$fdisplay variable format");
    $fclose(fd);
    if (errs == 0) $display("PASSED");
  end
endmodule
