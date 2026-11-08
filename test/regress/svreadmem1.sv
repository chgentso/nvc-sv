// Expected values: IEEE 1800-2017 21.4 ($readmemb/$readmemh file format: comments, white space, underscores, x/z
// digits, @address records, start/finish addresses including descending order) and 21.4.2 ($writemem: the written file
// is readable by $readmem; checked by round trip, not by file layout).
// Left out (implementation-defined, 21.4 is silent): words wider than the element, padding of narrower words that start
// with x/z, whether loading stops or continues after a word beyond the finish address, the default direction for a
// descending-declared array, and the finish address default for a descending array.  Where the continuation matters the
// checks accept either outcome.
module svreadmem1;
  reg [15:0] up [0:5];
  reg [15:0] dn [5:0];
  reg [15:0] off [10:13];
  reg [15:0] t6 [0:5];
  reg [7:0] b8 [0:3];
  reg [3:0] n4 [0:3];
  int err = 0;
  localparam logic [15:0] X = 16'hxxxx;
  function automatic void chk(bit ok, string m);
    if (!ok) begin $error("%s", m); err++; end
  endfunction
  task automatic put(string name, string text);
    int f;
    f = $fopen(name, "w"); $fwrite(f, "%s", text); $fclose(f);
  endtask
  task automatic clr();
    foreach (up[i]) up[i] = 16'hxxxx;
    foreach (dn[i]) dn[i] = 16'hxxxx;
    foreach (off[i]) off[i] = 16'hxxxx;
  endtask
  function automatic bit up_is(logic [15:0] e0, e1, e2, e3, e4, e5);
    return up[0] === e0 && up[1] === e1 && up[2] === e2 && up[3] === e3 && up[4] === e4 && up[5] === e5;
  endfunction
  function automatic bit same_up_t6();
    foreach (up[i]) if (up[i] !== t6[i]) return 0;
    return 1;
  endfunction
  initial begin
    put("/tmp/rm_a.txt", "// comment\ndead BEEF 0123\n/* block */ 4 5_6\n@4 ff\nabcd\n");
    clr(); $readmemh("/tmp/rm_a.txt", up);
    chk(up_is(16'hdead, 16'hbeef, 16'h0123, 16'h0004, 16'h00ff, 16'habcd), "up default");
    // descending-declared array without addresses: direction is not fixed; accept either full result
    clr(); $readmemh("/tmp/rm_a.txt", dn);
    chk((dn[0] === 16'hdead && dn[1] === 16'hbeef && dn[2] === 16'h0123 && dn[3] === 16'h0004 &&
         dn[4] === 16'h00ff && dn[5] === 16'habcd) ||
        (dn[5] === 16'hdead && dn[4] === 16'h00ff && dn[3] === 16'habcd && dn[2] === 16'h0004 &&
         dn[1] === 16'h0056 && dn[0] === X), "dn default");
    clr(); $readmemh("/tmp/rm_a.txt", off);
    chk(off[10] === 16'hdead && off[11] === 16'hbeef && off[12] === 16'h0123 && off[13] === 16'h0004, "off default");
    clr(); $readmemh("/tmp/rm_a.txt", up, 2);
    chk(up[0] === X && up[1] === X && up[2] === 16'hdead && up[3] === 16'hbeef &&
        (up[4] === 16'h0123 || up[4] === 16'h00ff) && (up[5] === 16'h0004 || up[5] === 16'habcd), "up start 2");
    clr(); $readmemh("/tmp/rm_a.txt", up, 1, 3);
    chk(up_is(X, 16'hdead, 16'hbeef, 16'h0123, X, X), "up 1..3");
    clr(); $readmemh("/tmp/rm_a.txt", up, 4, 2);
    chk(up[0] === X && up[1] === X && up[2] === 16'h0123 && (up[3] === 16'hbeef || up[3] === 16'habcd) &&
        (up[4] === 16'hdead || up[4] === 16'h00ff) && up[5] === X, "up 4 down to 2");
    clr(); $readmemh("/tmp/rm_a.txt", dn, 5, 3);
    chk(dn[0] === X && dn[1] === X && dn[2] === X && (dn[3] === 16'h0123 || dn[3] === 16'habcd) &&
        (dn[4] === 16'hbeef || dn[4] === 16'h00ff) && dn[5] === 16'hdead, "dn 5 down to 3");
    clr(); $readmemh("/tmp/rm_a.txt", dn, 1);
    clr(); $readmemh("/tmp/rm_a.txt", off, 12);
    chk(off[10] === X && off[11] === X && off[12] === 16'hdead && off[13] === 16'hbeef, "off start 12");
    clr(); $readmemh("/tmp/rm_a.txt", off, 11, 12);
    chk(off[10] === X && off[11] === 16'hdead && off[12] === 16'hbeef && off[13] === X, "off 11..12");

    // binary file; the first two words have a defined value; later words are narrower/beyond the array (not checked)
    put("/tmp/rm_b.txt", "1010 0101_1111 x01z zz 1\n@2\n11110000\n");
    $readmemb("/tmp/rm_b.txt", b8);
    chk(b8[0] === 8'b00001010 && b8[1] === 8'b01011111, "readmemb leading words");
    // exact-width words with x/z, address record
    put("/tmp/rm_b2.txt", "1x0z1x0z 00001111\n@3 10101010\n");
    foreach (b8[i]) b8[i] = 8'h77;
    $readmemb("/tmp/rm_b2.txt", b8);
    chk(b8[0] === 8'b1x0z1x0z && b8[1] === 8'b00001111 && b8[2] === 8'h77 && b8[3] === 8'b10101010, "readmemb x/z, @3");
    put("/tmp/rm_c.txt", "123 45 6\nf\n");   // first word wider than the element: not checked
    $readmemh("/tmp/rm_c.txt", n4);
    put("/tmp/rm_c2.txt", "x 0 z f\n");
    $readmemh("/tmp/rm_c2.txt", n4);
    chk(n4[0] === 4'hx && n4[1] === 4'h0 && n4[2] === 4'hz && n4[3] === 4'hf, "readmemh single digit words");

    put("/tmp/rm_d.txt", "@3 1 2 3 4\n@0 9\n");
    clr(); $readmemh("/tmp/rm_d.txt", up);
    chk(up[1] === X && up[2] === X && up[3] === 16'h1 && up[4] === 16'h2 && up[5] === 16'h3 &&
        (up[0] === X || up[0] === 16'h9), "@3 then overflow");
    put("/tmp/rm_e.txt", "XX zz Xz 12\n");
    clr(); $readmemh("/tmp/rm_e.txt", up);
    chk(up[0][7:0] === 8'bxxxxxxxx && up[1][7:0] === 8'bzzzzzzzz && up[2][7:0] === 8'bxxxxzzzz && up[3] === 16'h0012 &&
        up[4] === X && up[5] === X, "x/z digits");
    put("/tmp/rm_f.txt", "a b c\td\ne\n");
    clr(); $readmemh("/tmp/rm_f.txt", up);
    chk(up_is(16'ha, 16'hb, 16'hc, 16'hd, 16'he, X), "space/tab/newline separators");
    put("/tmp/rm_g.txt", "@1\n");
    clr(); $readmemh("/tmp/rm_g.txt", up);
    chk(up_is(X, X, X, X, X, X), "address only");
    put("/tmp/rm_h.txt", "");
    clr(); up[0] = 16'h1; $readmemh("/tmp/rm_h.txt", up);
    chk(up_is(16'h1, X, X, X, X, X), "empty file");
    put("/tmp/rm_i.txt", "@8 1\n");
    clr(); $readmemh("/tmp/rm_i.txt", up);
    chk(up_is(X, X, X, X, X, X), "address out of range loads nothing");
    put("/tmp/rm_j.txt", "10000 1 2\n");      // word wider than the element: result not checked
    clr(); $readmemh("/tmp/rm_j.txt", up);

    // $writemem round trips (21.4.2: the file can be read back with $readmem)
    put("/tmp/rm_a.txt", "dead beef 0123 0004 00ff abcd\n");
    clr(); $readmemh("/tmp/rm_a.txt", up);
    foreach (up[i]) t6[i] = up[i];
    $writememh("/tmp/rm_w1.txt", up);
    clr(); $readmemh("/tmp/rm_w1.txt", up);
    chk(same_up_t6(), "writememh/readmemh round trip");
    $writememb("/tmp/rm_w1b.txt", up);
    clr(); $readmemb("/tmp/rm_w1b.txt", up);
    chk(same_up_t6(), "writememb/readmemb round trip");
    $writememh("/tmp/rm_w3.txt", up, 1, 3);
    clr(); $readmemh("/tmp/rm_w3.txt", up, 1, 3);
    chk(up_is(X, 16'hbeef, 16'h0123, 16'h0004, X, X), "writememh start/finish");
    clr(); $writememh("/tmp/rm_w0.txt", up);           // all-x contents
    foreach (up[i]) up[i] = 16'h0;
    $readmemh("/tmp/rm_w0.txt", up);
    chk(up_is(X, X, X, X, X, X), "x round trip");
    foreach (off[i]) off[i] = 16'h1000 + i;
    $writememh("/tmp/rm_w4.txt", off);
    foreach (off[i]) off[i] = 16'h0;
    $readmemh("/tmp/rm_w4.txt", off);
    chk(off[10] === 16'h100a && off[11] === 16'h100b && off[12] === 16'h100c && off[13] === 16'h100d, "off round trip");
    for (int i = 0; i <= 5; i++) dn[i] = 16'h2000 + i;
    $writememh("/tmp/rm_w5.txt", dn);
    foreach (dn[i]) dn[i] = 16'h0;
    $readmemh("/tmp/rm_w5.txt", dn);
    chk(dn[0] === 16'h2000 && dn[1] === 16'h2001 && dn[2] === 16'h2002 && dn[3] === 16'h2003 && dn[4] === 16'h2004 &&
        dn[5] === 16'h2005, "dn round trip");
    foreach (n4[i]) n4[i] = i;
    $writememh("/tmp/rm_w7.txt", n4);
    $writememb("/tmp/rm_w7b.txt", n4);
    foreach (n4[i]) n4[i] = 4'hf;
    $readmemh("/tmp/rm_w7.txt", n4);
    chk(n4[0] === 4'h0 && n4[1] === 4'h1 && n4[2] === 4'h2 && n4[3] === 4'h3, "n4 hex round trip");
    foreach (n4[i]) n4[i] = 4'hf;
    $readmemb("/tmp/rm_w7b.txt", n4);
    chk(n4[0] === 4'h0 && n4[1] === 4'h1 && n4[2] === 4'h2 && n4[3] === 4'h3, "n4 binary round trip");
    foreach (b8[i]) b8[i] = 8'h0 + i * 3;
    b8[2] = 8'hxz;
    $writememh("/tmp/rm_w8.txt", b8);
    $writememb("/tmp/rm_w9.txt", b8);
    foreach (b8[i]) b8[i] = 8'h55;
    $readmemh("/tmp/rm_w8.txt", b8);
    chk(b8[0] === 8'h00 && b8[1] === 8'h03 && b8[2] === 8'hxz && b8[3] === 8'h09, "b8 hex round trip with x/z");
    foreach (b8[i]) b8[i] = 8'h55;
    $readmemb("/tmp/rm_w9.txt", b8);
    chk(b8[0] === 8'h00 && b8[1] === 8'h03 && b8[2] === 8'hxz && b8[3] === 8'h09, "b8 binary round trip with x/z");
    if (err == 0) $display("PASSED");
  end
endmodule
