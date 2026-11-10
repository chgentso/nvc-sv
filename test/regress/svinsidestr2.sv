// An integral value (one character of a string, a byte) in inside with a queue or dynamic array
// of strings is compared as a string (IEEE 1800-2017 11.4.13, 6.16: an integral operand compared
// with a string is converted to a string).  Found in riscv-dv: "csr_name[0] inside {invalid_lvl}".
`define CHK(c, m) if (!(c)) begin err++; $error(m); end
module svinsidestr2;
  int err = 0;
  string q[$];
  string d[];
  string n = "MSTATUS";
  string x = "XSTATUS";
  string s;
  byte b;
  initial begin
    q.push_back("D");
    q.push_back("M");
    d = new[2];
    d[0] = "S";
    d[1] = "AB";
    `CHK((n[0] inside {q}) == 1, "M in queue")
    `CHK((x[0] inside {q}) == 0, "X not in queue")
    b = "D";
    `CHK((b inside {q}) == 1, "byte D in queue")
    b = "S";
    `CHK((b inside {q}) == 0, "byte S not in queue")
    `CHK((b inside {d}) == 1, "byte S in dynamic array")
    `CHK((b inside {q, d}) == 1, "two arrays")
    `CHK((b inside {"X", "S"}) == 1, "string literals")
    s = "AB";
    `CHK((s inside {d}) == 1, "string in array")
    `CHK((s inside {q}) == 0, "string not in array")
    `CHK((s inside {q, "AB"}) == 1, "array and literal")
    if (err == 0) $display("PASSED");
    else $fatal(1, "FAILED");
  end
endmodule
