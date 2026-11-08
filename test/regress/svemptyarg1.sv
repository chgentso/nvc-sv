// Empty arguments in display/write argument lists.
// Expected values: IEEE 1800-2017 21.2.1 and A.8.3 (list_of_arguments allows empty
// arguments; an empty argument prints as a single space), 21.2.1.? format specifiers
// consume arguments in order, 21.3.3 ($swrite formats like $write).
`define CHK(c, m) if (!(c)) begin err++; $error(m); end
module svemptyarg1;
  string s;
  int err = 0;
  initial begin
    // the output of each argument list is captured in a string and compared exactly
    $swrite(s, "%0d [%b]", 5, 3'b101,);
    `CHK(s == "5 [101] ", "trailing empty argument after format arguments")
    $swrite(s, "a",,"b");
    `CHK(s == "a b", "empty argument between strings")
    $swrite(s, "x",,"\n");
    `CHK(s == "x \n", "empty argument before newline string")
    $swrite(s, ,"lead");
    `CHK(s == " lead", "leading empty argument")
    $swrite(s, "end",);
    `CHK(s == "end ", "trailing empty argument")
    $swrite(s, "[%0d]", 7,,);
    `CHK(s == "[7]  ", "two trailing empty arguments")
    // the tasks themselves must accept the same forms
    $display("%0d [%b]", 5, 3'b101,);
    $display("a",,"b");
    $write("x",,"\n");
    $display(,"lead");
    $display("end",);
    $display("[%0d]", 7,,);
    if (err == 0) $display("PASSED");
  end
endmodule
