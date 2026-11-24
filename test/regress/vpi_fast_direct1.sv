`timescale 1ns/1ns

module vpi_fast_direct1;
   logic [7:0]  sig_fast;
   bit   [31:0] sig_b32;

   initial begin
      // Exercise fast-direct inline writes while completely unwatched
      sig_fast = 8'h11;
      sig_fast = 8'h22;
      sig_fast = 8'h5a;
      sig_b32  = 32'h0000_0001;
      sig_b32  = 32'h1234_5678;

      #15;
      // VPI cbValueChange attached at 10ns; no-op must NOT fire callback
      sig_fast = 8'h5a;
      #5;
      // Changed write MUST fire callback once
      sig_fast = 8'h77;
      #5; // 25ns: after VPI force at 24ns, deposit must be masked
      sig_fast = 8'haa;
      #2;
      if (sig_fast !== 8'h3c) $fatal(1, "sig_fast forced=%h", sig_fast);
      #8; // 35ns: after VPI release at 30ns, direct fast write restored
      sig_fast = 8'h99;
      sig_fast = 8'h99;
      #5;
      if (sig_fast !== 8'h99) $fatal(1, "sig_fast=%h", sig_fast);
   end
endmodule
