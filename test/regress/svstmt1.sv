// unique, unique0 and priority, case inside, do while, non-blocking trigger
module svstmt1;
   `define CHECK(x, y) if ((x) !== (y)) begin \
      $display("FAILED %0d: %0d", y, (x)); $finish; end

   int a, b, n;
   event e, e2;
   int hits = 0;
   int sel = 0;
   logic ins;
   always @* ins = sel inside {1, [4:6]};

   always @(e) hits++;
   always @(e2) hits += 10;

   initial begin
      a = 1; b = 0;
      unique if (a == 1) b = 1; else if (a == 2) b = 2;
      `CHECK(b, 1);
      unique if (a == 1) b = 3; else if (a == 1) b = 4;
      `CHECK(b, 3);
      unique if (a == 5) b = 5; else if (a == 6) b = 6;
      `CHECK(b, 3);
      unique0 if (a == 5) b = 5; else if (a == 6) b = 6;
      `CHECK(b, 3);
      unique0 if (a == 1) b = 7; else if (a == 1) b = 8;
      `CHECK(b, 7);
      priority if (a == 5) b = 9; else if (a == 6) b = 10;
      `CHECK(b, 7);
      priority if (a == 1) b = 11; else b = 12;
      `CHECK(b, 11);

      unique case (a) 1: b = 20; 2: b = 21; endcase
      `CHECK(b, 20);
      unique case (a) 1: b = 22; 1: b = 23; endcase
      `CHECK(b, 22);
      unique case (a) 5: b = 24; default: b = 25; endcase
      `CHECK(b, 25);
      unique0 case (a) 5: b = 26; 6: b = 27; endcase
      `CHECK(b, 25);
      priority case (a) 1: b = 28; 1: b = 29; endcase
      `CHECK(b, 28);
      priority case (a) 7: b = 30; endcase
      `CHECK(b, 28);

      for (int i = 0; i < 9; i++) begin
         case (i) inside
            1, 3: b = 1;
            4'b01??, [5:6]: b = 2;
            [8:9]: b = 3;
            default: b = 0;
         endcase
         if (i == 0) `CHECK(b, 0);
         if (i == 1) `CHECK(b, 1);
         if (i == 2) `CHECK(b, 0);
         if (i == 3) `CHECK(b, 1);
         if (i == 4) `CHECK(b, 2);
         if (i == 5) `CHECK(b, 2);
         if (i == 7) `CHECK(b, 2);
         if (i == 8) `CHECK(b, 3);
      end

      unique case (a) inside
         [0:3]: b = 40;
         [2:4]: b = 41;
      endcase
      `CHECK(b, 40);

      n = 0;
      do begin n++; end while (n < 5);
      `CHECK(n, 5);
      do n = 10; while (0);
      `CHECK(n, 10);

      ->>e;
      #1;
      `CHECK(hits, 1);
      ->e;
      -> e2;
      #1;
      `CHECK(hits, 12);

      sel = 5;
      #1;
      `CHECK(ins, 1'b1);
      sel = 2;
      #1;
      `CHECK(ins, 1'b0);
      $display("PASSED");
   end
endmodule
