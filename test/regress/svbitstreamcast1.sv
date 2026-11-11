// Casting between dynamic arrays and queues with elements of different
// widths converts the stream of bits (IEEE 1800-2017 6.24.3)
module svbitstreamcast1;
   typedef bit bit_arr_t[];
   typedef byte unsigned byte_arr_t[];
   typedef byte unsigned byte_q_t[$];
   typedef bit [15:0] half_arr_t[];

   function automatic string bits(bit_arr_t a);
      string s = "";
      foreach (a[i]) s = {s, a[i] ? "1" : "0"};
      return s;
   endfunction

   initial begin
      byte unsigned bytes[] = '{8'h80, 8'h06, 8'h01};
      byte unsigned bq[$] = '{8'ha5, 8'h3c};
      bit_arr_t b;
      byte_arr_t by;
      half_arr_t h;
      bit_arr_t src;

      b = bit_arr_t'(bytes);
      if (bits(b) != "100000000000011000000001")
         $fatal(1, "bytes to bits %s", bits(b));

      b = bit_arr_t'(bq);
      if (bits(b) != "1010010100111100") $fatal(1, "queue to bits %s", bits(b));

      src = new[16];
      for (int i = 0; i < 16; i++) src[i] = (16'h1234 >> (15 - i)) & 1;
      by = byte_arr_t'(src);
      if (by.size() != 2 || by[0] !== 8'h12 || by[1] !== 8'h34)
         $fatal(1, "bits to bytes %0d %h %h", by.size(), by[0], by[1]);

      bytes = '{8'h80, 8'h06, 8'h01, 8'hff};
      h = half_arr_t'(bytes);
      if (h.size() != 2 || h[0] !== 16'h8006 || h[1] !== 16'h01ff)
         $fatal(1, "bytes to halfwords %0d", h.size());

      begin
         byte unsigned fixed[3] = '{8'h80, 8'h06, 8'h01};
         bit [15:0] hf[2] = '{16'h8006, 16'h01ff};
         byte unsigned dyn[] = '{8'h80, 8'h06, 8'h01, 8'hff};

         b = bit_arr_t'(fixed);
         if (bits(b) != "100000000000011000000001")
            $fatal(1, "fixed to bits %s", bits(b));

         b = bit_arr_t'(dyn[0:1]);
         if (bits(b) != "1000000000000110")
            $fatal(1, "slice to bits %s", bits(b));

         h = half_arr_t'(dyn[0:1]);
         if (h.size() != 1 || h[0] !== 16'h8006)
            $fatal(1, "slice to halfwords");

         by = byte_arr_t'(hf);
         if (by.size() != 4 || by[0] !== 8'h80 || by[1] !== 8'h06
             || by[2] !== 8'h01 || by[3] !== 8'hff)
            $fatal(1, "fixed halfwords to bytes");

         by = byte_arr_t'(fixed);
         if (by.size() != 3 || by[2] !== 8'h01)
            $fatal(1, "fixed to bytes");
      end

      $display("PASSED");
   end
endmodule
