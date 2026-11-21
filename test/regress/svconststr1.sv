module svconststr1;
  parameter string s1 = "Hello";
  parameter string s2 = "hello";
  parameter byte c1 = s1.getc(1);
  parameter int cmp = s1.compare(s2);
  parameter int icmp = s1.icompare(s2);
  parameter string up = s1.toupper();
  parameter string lo = s1.tolower();

  initial begin
    if (c1 !== "e") $fatal(1, "getc mismatch");
    if (cmp >= 0) $fatal(1, "compare mismatch");
    if (icmp !== 0) $fatal(1, "icompare mismatch");
    if (up != "HELLO") $fatal(1, "toupper mismatch");
    if (lo != "hello") $fatal(1, "tolower mismatch");
    $display("PASSED");
  end
endmodule
