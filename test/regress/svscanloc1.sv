// Expected values: IEEE 1800-2017 21.3.4.3 ($fscanf/$sscanf: %d, %h, %b, %o, %s; %d also matches a single x/X/z/Z/?;
// return value = number of items assigned), 21.3.4 ($fscanf/$feof), 11.8 (assignment truncation/extension),
// 6.8 (2-state variables take 0 for z)
typedef struct { int a; logic [7:0] b; } st_t;
class C;
   int p;
   bit [11:0] q;
endclass

module svscanloc1;
   int g;
   int arr[3];
   st_t st;
   C c;
   logic [15:0] w;
   logic [3:0] four;
   reg signed [7:0] sg;
   integer wide;
   int unsigned neg;
   int err = 0;

   function automatic void chk(bit ok, string m);
      if (!ok) begin $error("%s", m); err++; end
   endfunction

   function automatic int f(string s);
      int x;
      int r;
      r = $sscanf(s, "%d", x);
      return x + r;
   endfunction

   task automatic t;
      int c1; int r; logic [9:0] c2;
      r = $sscanf("9 1f", "%d %h", c1, c2);
      chk(r == 2 && c1 == 9 && c2 === 10'h01f, "task locals");
   endtask

   initial begin
      int a; int r; string s; reg [7:0] b8; bit [3:0] nib;
      r = $sscanf("5", "%d", a); chk(r == 1 && a == 5, "local int");
      r = $sscanf("hello", "%s", s); chk(r == 1 && s == "hello", "local string");
      r = $sscanf("ff 101 17", "%h %b %o", b8, nib, wide);
      chk(r == 3 && b8 === 8'hff && nib === 4'b0101 && wide == 15, "multi");
      begin
         int b;
         r = $sscanf("7", "%d", b); chk(r == 1 && b == 7, "nested");
      end
      r = $sscanf("11", "%d", g); chk(r == 1 && g == 11, "module var");
      r = $sscanf("1 2 3", "%d %d %d", arr[0], arr[1], arr[2]);
      chk(r == 3 && arr[0] == 1 && arr[1] == 2 && arr[2] == 3, "array elements");
      r = $sscanf("12 ab", "%d %h", st.a, st.b); chk(r == 2 && st.a == 12 && st.b === 8'hab, "struct members");
      c = new;
      r = $sscanf("33 fff", "%d %h", c.p, c.q); chk(r == 2 && c.p == 33 && c.q === 12'hfff, "class members");
      r = $sscanf("bead", "%h", w); chk(r == 1 && w === 16'hbead, "logic16");
      r = $sscanf("-3", "%d", sg); chk(r == 1 && sg == -3, "signed");
      r = $sscanf("-1", "%d", neg); chk(r == 1 && neg == 32'hffffffff, "unsigned negative");
      r = $sscanf("300", "%d", b8); chk(r == 1 && b8 === 8'd44, "truncation to 8 bits");
      a = 77;
      r = $sscanf("zz", "%d", a); chk(r == 1 && a == 0, "z matches %d, 2-state target gets 0");
      r = $sscanf("4 zz", "%d %d", a, g); chk(r == 2 && a == 4 && g == 0, "partial");
      chk(f("40") == 41, "function local");
      r = $sscanf("1010", "%b", four); chk(r == 1 && four === 4'b1010, "nibble");
   end
   initial #1 t();
   initial begin
      int fd; int v1, v2; string cmd; logic [31:0] ad; int r; automatic int n = 0;
      #2;
      fd = $fopen("svscanloc1.txt", "w");
      $fdisplay(fd, "load 00000010 deadbeef");
      $fdisplay(fd, "load 00000014 12345678");
      $fclose(fd);
      fd = $fopen("svscanloc1.txt", "r");
      while (!$feof(fd)) begin
         logic [31:0] dd;
         r = $fscanf(fd, "%s %08x %08x", cmd, ad, dd);
         if (r == 3) begin
            if (n == 0) chk(cmd == "load" && ad === 32'h10 && dd === 32'hdeadbeef, "file line 1");
            if (n == 1) chk(cmd == "load" && ad === 32'h14 && dd === 32'h12345678, "file line 2");
            n++;
         end
      end
      $fclose(fd);
      chk(n == 2, "two lines scanned");
      if (err == 0) $display("PASSED");
   end
endmodule
