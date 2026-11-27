module svpartdyn1;
   function automatic [7:0] wr_part(input [95:0] idx);
      reg [7:0] v;
      v = 8'hff;
      v[idx +: 4] = 4'h0;
      return v;
   endfunction

   function automatic [7:0] wr_signed(input signed [95:0] idx);
      reg [7:0] v;
      v = 8'hff;
      v[idx +: 4] = 4'h0;
      return v;
   endfunction

   function automatic [7:0] op_ret(input int sel);
      op_ret = 8'ha5;
      op_ret[sel +: 4] += 1;
   endfunction

   localparam [7:0] P_HI  = wr_part(96'd6);
   localparam [7:0] P_OOB = wr_part(96'h10000000000000001);
   localparam [7:0] P_NEG = wr_part(-96'd2);
   localparam [7:0] P_SNG = wr_signed(-96'sd2);

   initial begin
      if (P_HI !== 8'h3f)  $fatal(1, "P_HI=%h", P_HI);
      if (P_OOB !== 8'hff) $fatal(1, "P_OOB=%h", P_OOB);
      if (P_NEG !== 8'hff) $fatal(1, "P_NEG=%h", P_NEG);
      if (P_SNG !== 8'hfc) $fatal(1, "P_SNG=%h", P_SNG);

      if (op_ret(0) !== 8'ha6)       $fatal(1, "op_ret(0)=%b", op_ret(0));
      if (op_ret(-2) !== 8'b101001xx) $fatal(1, "op_ret(-2)=%b", op_ret(-2));
      if (op_ret(8) !== 8'ha5)       $fatal(1, "op_ret(8)=%b", op_ret(8));

      // Wide 65-bit overflow index into a dynamic array must be out of range
      begin
         automatic int da[] = '{11, 22};
         automatic logic [64:0] big = 65'h10000000000000000;
         if (da[big] !== 0) $fatal(1, "da[big]=%0d", da[big]);
      end

      $display("PASSED");
   end
endmodule
