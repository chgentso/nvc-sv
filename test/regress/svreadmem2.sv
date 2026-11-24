// Bulk $readmemh/$readmemb across large ascending/descending 4-state,
// 2-state, wide (>64-bit) and memory-backed arrays with @addr jumps,
// overwrites, partial loads, x/z values and reverse start/finish bounds.

module svreadmem2;
   localparam int N = 4096;

   logic [31:0]  mem4 [0:N-1];
   bit   [31:0]  mem2 [0:N-1];
   logic [31:0]  desc [N-1:0];
   logic [95:0]  wide [0:15];
   logic [7:0]   binm [0:63];

   int err = 0;

   function automatic void chk(bit ok, string msg);
      if (!ok) begin
         $error("%s", msg);
         err++;
      end
   endfunction

   initial begin
      int f;

      // 1. Large 4096-word hex file with comments, underscores, @addr jumps,
      //    and duplicate address overwrite
      f = $fopen("rm2_big.hex", "w");
      $fwrite(f, "// large ROM image\n/* block comment */\n");
      for (int i = 0; i < N; i++)
         $fwrite(f, "%04x_%04x\n", (i * 3) & 16'hffff, (i ^ 16'h5a5a) & 16'hffff);
      $fwrite(f, "@0010 dead_beef\n@0fff cafe_babe\n");
      $fclose(f);

      $readmemh("rm2_big.hex", mem4);
      $readmemh("rm2_big.hex", desc);
      for (int i = 0; i < N; i++) begin
         automatic logic [31:0] exp = (i == 16) ? 32'hdead_beef
                          : (i == N - 1) ? 32'hcafe_babe
                          : {16'((i * 3) & 16'hffff), 16'((i ^ 16'h5a5a) & 16'hffff)};
         chk(mem4[i] === exp, $sformatf("mem4[%0d]=%h exp=%h", i, mem4[i], exp));
         chk(desc[i] === exp, $sformatf("desc[%0d]=%h exp=%h", i, desc[i], exp));
      end

      // 2. Partial load with reverse start/finish on 2-state memory + x/z clamping
      for (int i = 0; i < 16; i++) mem2[i] = 32'h1111_1111;
      f = $fopen("rm2_2s.hex", "w");
      $fwrite(f, "xxxx_0001 zzzz_0002 1234_5678\n");
      $fclose(f);
      $readmemh("rm2_2s.hex", mem2, 5, 3);
      chk(mem2[2] === 32'h1111_1111, "mem2[2] untouched");
      chk(mem2[5] === 32'h0000_0001, "mem2[5] x clamped to 0");
      chk(mem2[4] === 32'h0000_0002, "mem2[4] z clamped to 0");
      chk(mem2[3] === 32'h1234_5678, "mem2[3]");
      chk(mem2[6] === 32'h1111_1111, "mem2[6] untouched");

      // 3. Wide (>64-bit) memory with x/z extension
      for (int i = 0; i < 16; i++) wide[i] = '0;
      f = $fopen("rm2_wide.hex", "w");
      $fwrite(f, "@2 01234567_89abcdef_fedcba98\nx5\n");
      $fclose(f);
      $readmemh("rm2_wide.hex", wide);
      chk(wide[1] === 96'h0, "wide[1] untouched");
      chk(wide[2] === 96'h01234567_89abcdef_fedcba98, "wide[2]");
      chk(wide[3][7:0] === 8'hx5 && wide[3][95:8] === {88{1'bx}}, "wide[3] x-ext");

      // 4. $readmemb with @addr and x/z
      for (int i = 0; i < 64; i++) binm[i] = 8'h55;
      f = $fopen("rm2_bin.txt", "w");
      $fwrite(f, "@4 1010_0101 1x0z_1x0z\n@3f 00001111\n");
      $fclose(f);
      $readmemb("rm2_bin.txt", binm);
      chk(binm[3] === 8'h55, "binm[3] untouched");
      chk(binm[4] === 8'b1010_0101, "binm[4]");
      chk(binm[5] === 8'b1x0z_1x0z, "binm[5]");
      chk(binm[63] === 8'b0000_1111, "binm[63]");

      if (err == 0) $display("PASSED");
   end
endmodule
