module countbits1;
   logic [7:0] pv;
   logic       uv[4];
   real        r;
   string      s;
   int         c;
   logic       b;

   initial begin
      c = $countones(pv);         // OK: packed vector
      c = $countbits(pv, 1'b1);   // OK
      b = $onehot(pv);            // OK
      b = $onehot0(pv);           // OK
      b = $isunknown(pv);         // OK
      c = $countones(uv);         // Error: unpacked array
      c = $countbits(uv, 1'b0);   // Error: unpacked array
      b = $onehot(uv);            // Error: unpacked array
      b = $onehot0(r);            // Error: real
      b = $isunknown(s);          // Error: string
   end
endmodule
