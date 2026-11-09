// Run-time bit, part and indexed part selects of wide packed arrays, read
// and written, for four-state and two-state, signed and unsigned, in a
// class member, an automatic variable, a structure field and a module
// variable.  An out of range read gives x (four-state) or 0 (two-state),
// an out of range write is ignored (IEEE 1800-2017 11.5.1, 11.5.2).

module svwidesel2;
   typedef struct {
      logic [3:0][79:0] l;
      bit signed [3:0][79:0] b;
   } st_t;

   class C;
      logic [3:0][79:0] l;
      logic signed [3:0][79:0] ls;
      bit [3:0][79:0] b;
      bit signed [3:0][79:0] bs;
      logic [3:0][2:0][39:0] m;
   endclass

   logic [3:0][79:0] ml;
   bit [3:0][79:0] mb;

   int errors = 0;
   `define CHECK(c, msg) if (!(c)) begin errors++; $display("FAIL %s", msg); end

   initial begin
      automatic C c = new;
      automatic st_t s;
      automatic logic [3:0][79:0] al;
      automatic int i;
      automatic logic [2:0] xi = 3'bx;
      automatic logic [79:0] e4;
      automatic bit [79:0] e2;
      automatic logic signed [79:0] es;

      for (i = 0; i < 4; i++) begin
         c.l[i] = 80'hf0 + i;  c.ls[i] = -80'sd5 - i;
         c.b[i] = 80'h70 + i;  c.bs[i] = -80'sd9 - i;
         s.l[i] = 80'h30 + i;  s.b[i] = -80'sd2 - i;
         al[i] = 80'h10 + i;   ml[i] = 80'h20 + i;  mb[i] = 80'h40 + i;
      end

      for (i = 0; i < 4; i++) begin
         `CHECK(c.l[i] === 80'hf0 + i, "c.l in range")
         `CHECK(c.ls[i] === -80'sd5 - i, "c.ls in range")
         `CHECK(c.b[i] === 80'h70 + i, "c.b in range")
         `CHECK(c.bs[i] === -80'sd9 - i, "c.bs in range")
         `CHECK(s.l[i] === 80'h30 + i, "s.l in range")
         `CHECK(s.b[i] === -80'sd2 - i, "s.b in range")
         `CHECK(al[i] === 80'h10 + i, "al in range")
         `CHECK(ml[i] === 80'h20 + i, "ml in range")
         `CHECK(mb[i] === 80'h40 + i, "mb in range")
      end

      // Out of range and unknown reads
      i = 4;
      e4 = c.l[i];  `CHECK(e4 === 80'bx, "c.l[4]")
      es = c.ls[i]; `CHECK(es === 80'bx, "c.ls[4]")
      e2 = c.b[i];  `CHECK(e2 === 80'b0, "c.b[4]")
      e2 = c.bs[i]; `CHECK(e2 === 80'b0, "c.bs[4]")
      e4 = s.l[i];  `CHECK(e4 === 80'bx, "s.l[4]")
      e2 = s.b[i];  `CHECK(e2 === 80'b0, "s.b[4]")
      e4 = al[i];   `CHECK(e4 === 80'bx, "al[4]")
      e4 = ml[i];   `CHECK(e4 === 80'bx, "ml[4]")
      e2 = mb[i];   `CHECK(e2 === 80'b0, "mb[4]")
      i = -1;
      e4 = c.l[i];  `CHECK(e4 === 80'bx, "c.l[-1]")
      e2 = c.b[i];  `CHECK(e2 === 80'b0, "c.b[-1]")
      e4 = c.l[xi]; `CHECK(e4 === 80'bx, "c.l[x]")
      e2 = c.b[xi]; `CHECK(e2 === 80'b0, "c.b[x]")
      e4 = ml[xi];  `CHECK(e4 === 80'bx, "ml[x]")

      // Two dimensional selects
      for (i = 0; i < 4; i++)
         `CHECK(c.l[i][7:0] === 8'hf0 + i, "c.l[i][7:0]")
      i = 2;
      `CHECK(c.l[i][4] === 1'b1, "c.l[2][4]")
      i = 79;
      `CHECK(c.l[1][i] === 1'b0, "c.l[1][79]")
      i = 80;
      `CHECK(c.l[1][i] === 1'bx, "c.l[1][80]")
      `CHECK(c.b[1][i] === 1'b0, "c.b[1][80]")
      i = 3;
      for (int j = 0; j < 3; j++) c.m[i][j] = 40'(j + 1);
      for (int j = 0; j < 3; j++) `CHECK(c.m[i][j] === 40'(j + 1), "c.m[3][j]")
      i = 4;
      `CHECK(c.m[i][0] === 40'bx, "c.m[4][0]")

      // Indexed part selects
      c.l[0] = 80'h0123456789abcdef0123;
      i = 8;
      `CHECK(c.l[0][i +: 16] === 16'hef01, "+: in range")
      `CHECK(c.l[0][i + 15 -: 16] === 16'hef01, "-: in range")
      i = 70;
      `CHECK(c.l[0][i +: 16] === {6'bx, 10'h004}, "+: straddling top")
      i = 72;
      e4 = c.b[0][i +: 16];
      `CHECK(e4[15:0] === {8'h0, 8'h00}, "+: straddling top 2-state")

      // Writes: in range, out of range ignored
      i = 2;
      c.l[i] = 80'hdead;  `CHECK(c.l[2] === 80'hdead, "write c.l[2]")
      `CHECK(c.l[1] === 80'hf1 && c.l[3] === 80'hf3, "neighbours kept")
      c.b[i] = 80'hbeef;  `CHECK(c.b[2] === 80'hbeef, "write c.b[2]")
      s.l[i] = 80'h77;    `CHECK(s.l[2] === 80'h77, "write s.l[2]")
      al[i] = 80'h88;     `CHECK(al[2] === 80'h88, "write al[2]")
      ml[i] = 80'h99;     `CHECK(ml[2] === 80'h99, "write ml[2]")
      mb[i] = 80'h55;     `CHECK(mb[2] === 80'h55, "write mb[2]")
      i = 4;
      c.l[i] = 80'h1; c.b[i] = 80'h1; s.l[i] = 80'h1; al[i] = 80'h1;
      ml[i] = 80'h1; mb[i] = 80'h1;
      i = -1;
      c.l[i] = 80'h1; c.b[i] = 80'h1; ml[i] = 80'h1;
      c.l[xi] = 80'h1; c.b[xi] = 80'h1; ml[xi] = 80'h1;
      for (i = 0; i < 4; i++) begin
         `CHECK(c.l[i] === (i == 2 ? 80'hdead : i == 0 ? 80'h0123456789abcdef0123 : 80'hf0 + i), "c.l after oob write")
         `CHECK(ml[i] === (i == 2 ? 80'h99 : 80'h20 + i), "ml after oob write")
         `CHECK(mb[i] === (i == 2 ? 80'h55 : 80'h40 + i), "mb after oob write")
         `CHECK(al[i] === (i == 2 ? 80'h88 : 80'h10 + i), "al after oob write")
      end
      c.l[1][3 +: 8] = 8'hff;
      `CHECK(c.l[1] === 80'hf1 | (80'hff << 3), "write +:")
      i = 75;
      c.l[3][i +: 8] = 8'h00;   // Partly out of range: only the low bits
      `CHECK(c.l[3][79:75] === 5'b0, "write +: straddling")
      `CHECK(c.l[3][74:0] === 75'(80'hf3), "write +: straddling keeps low")
      c.l[3][i] = 1'b1;
      `CHECK(c.l[3][75] === 1'b1, "write bit")

      if (errors == 0) $display("PASSED");
      else $fatal(1, "%0d errors", errors);
   end
endmodule
