// Format specifiers are not case sensitive
// Expected values: IEEE 1800-2017 21.2.1.1 (format specifications are case insensitive: %H = %h, %X = %x, ...),
// 21.2.1.2 (%h of 32 bits has 8 digits), 21.2.1.7/21.2.1.8 (%s, %c). The case of the hex letters is not fixed by the
// LRM, so each upper-case specifier is compared with its lower-case twin, and the lower-case %x is
// compared case-insensitively with the expected digits.
module svfmtupper1;
  reg [31:0] r = 32'hdeadbeef;
  int errs = 0;
  task automatic eq(string got, string exp, string m);
    if (got != exp) begin $error("%s: got '%s' expected '%s'", m, got, exp); errs++; end
  endtask
  initial begin
    string t;
    $display("[%X][%H][%08X][%D][%B][%O][%S][%C][%F][%G]", r, r, r, 12, 5, 8, "ab", 65, 1.5, 2.5);
    $display("[%x][%h][%08x]", r, r, r);
    eq($sformatf("%X", r), $sformatf("%x", r), "%X vs %x");
    eq($sformatf("%H", r), $sformatf("%h", r), "%H vs %h");
    eq($sformatf("%08X", r), $sformatf("%08x", r), "%08X vs %08x");
    eq($sformatf("%D", 12), $sformatf("%d", 12), "%D vs %d");
    eq($sformatf("%B", 5), $sformatf("%b", 5), "%B vs %b");
    eq($sformatf("%O", 8), $sformatf("%o", 8), "%O vs %o");
    eq($sformatf("%S", "ab"), $sformatf("%s", "ab"), "%S vs %s");
    eq($sformatf("%C", 65), $sformatf("%c", 65), "%C vs %c");
    eq($sformatf("%F", 1.5), $sformatf("%f", 1.5), "%F vs %f");
    eq($sformatf("%G", 2.5), $sformatf("%g", 2.5), "%G vs %g");
    eq($sformatf("%S", "ab"), "ab", "%S");
    eq($sformatf("%C", 65), "A", "%C");
    eq($sformatf("%B", 5), "00000000000000000000000000000101", "%B of 32 bits");
    eq($sformatf("%O", 8), "00000000010", "%O of 32 bits");
    t = $sformatf("%x", r); eq(t.tolower(), "deadbeef", "%x");
    t = $sformatf("%X", r); eq(t.tolower(), "deadbeef", "%X");
    t = $sformatf("%08X", r); eq(t.tolower(), "deadbeef", "%08X");
    if (errs == 0) $display("PASSED");
  end
endmodule
