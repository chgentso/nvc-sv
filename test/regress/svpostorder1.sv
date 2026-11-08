// IEEE 1800-2017 21.2.2 and 21.2.3: $strobe and $monitor print in the
// Postponed region and the LRM gives no order between them.  nvc prints
// all of them in one first in, first out order: the order in which the
// strobe call was made or the change that the monitor reports happened.
module svpostorder1;
  int a, fd;
  string want[$] = '{
    "mon 0", "strobe0 0",                       // time 0
    "strobe1 1", "mon 1",                       // time 1
    "mon 2", "strobe2 2",                       // time 2
    "strobe3 3", "mon 3"                        // time 3
  };

  initial begin
    fd = $fopen("svpostorder1.out", "w");
    $fmonitor(fd, "mon %0d", a);
    $fstrobe(fd, "strobe0 %0d", a);
    #1 $fstrobe(fd, "strobe1 %0d", a);
    a = 1;
    #1 a = 2;
    $fstrobe(fd, "strobe2 %0d", a);
    #1 $fstrobe(fd, "strobe3 %0d", a);
    a = 3;
    #1 $fclose(fd);
    check;
  end

  task check;
    string line;
    automatic int n = 0, errors = 0;
    fd = $fopen("svpostorder1.out", "r");
    while ($fgets(line, fd)) begin
      if (line.len() > 0 && line[line.len() - 1] == "\n")
        line = line.substr(0, line.len() - 2);
      if (n >= want.size() || line != want[n]) begin
        $display("line %0d: '%s', expected '%s'", n, line,
                 n < want.size() ? want[n] : "");
        errors++;
      end
      n++;
    end
    $fclose(fd);
    if (n != want.size()) begin
      $display("%0d lines, expected %0d", n, want.size());
      errors++;
    end
    if (errors == 0) $display("PASSED");
  endtask
endmodule
