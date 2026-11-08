// Expected values: IEEE 1800-2017 21.3.1 ($fopen/$fclose), 21.3.3 ($fdisplay), 21.3.4.3 ($fscanf: %s stops
// at white space, %d skips leading white space, returns the number of items assigned),
// 21.3.4.2 ($fgets reads up to and including the newline, returns the number of characters read),
// 21.3.4.3 ($sscanf)
module svstrscan1;
  int fd, n, k;
  int errs = 0;
  string t, u, line;
  reg [63:0] r;
  task automatic ci(string w, int got, int exp);
    if (got != exp) begin $error("%s: got %0d expected %0d", w, got, exp); errs++; end
  endtask
  task automatic cs(string w, string got, string exp);
    if (got != exp) begin $error("%s: got \"%s\" expected \"%s\"", w, got, exp); errs++; end
  endtask
  initial begin
    fd = $fopen("fs.tmp", "w"); $fdisplay(fd, "abc 42 def"); $fdisplay(fd, "second line"); $fclose(fd);
    fd = $fopen("fs.tmp", "r");
    n = $fscanf(fd, "%s %d %s", t, k, u);
    ci("fscanf n", n, 3); cs("t", t, "abc"); ci("k", k, 42); cs("u", u, "def");
    // the newline after "def" is still unread
    n = $fgets(line, fd);
    ci("fgets1 n", n, 1); cs("fgets1", line, "\n");
    n = $fgets(line, fd);
    ci("fgets2 n", n, 12); cs("fgets2", line, "second line\n");
    $fclose(fd);
    n = $sscanf("hello 7", "%s %d", t, k);
    ci("sscanf n", n, 2); cs("sscanf t", t, "hello"); ci("sscanf k", k, 7);
    if (errs == 0) $display("PASSED");
  end
endmodule
