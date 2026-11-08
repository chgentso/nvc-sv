module svlrm_c6f;
  // Self-checking test. Expected values: IEEE 1800-2017 6.9 (vector types), 6.11 (integer types,
  // signed/unsigned), 7.4.1 (packed arrays), 11.5.1 (bit/part selects, indexed part selects),
  // 11.6 (expression bit lengths; system-task arguments are self-determined), 11.4.3 (arithmetic:
  // integer division truncates toward zero, % takes the sign of the first operand, power operator
  // rules of 11.4.3.1 incl. Table 11-4), 11.4.10 (shifts), 20.5 ($signed, $unsigned).
  logic [7:0] v8 = 8'hf0;
  logic signed [7:0] s8 = 8'hf0;
  reg [15:0] r16;
  logic [0:3] up = 4'b1010;
  logic [3:0] dn = 4'b1010;
  logic [1:0][3:0] pk = 8'hab;
  logic [3:0][1:0][1:0] pk3 = 16'h1b2d;
  int errs;
  function automatic void ck(bit ok, string m);
    if (!ok) begin errs++; $error("%s", m); end
  endfunction
  // checks the decimal text of a self-determined expression
  `define CKD(e, txt) ck($sformatf("%0d", e) == txt, `"e`")
  initial begin
    `CKD(v8, "240");
    `CKD(s8, "-16");
    `CKD(v8 >> 2, "60");
    `CKD(s8 >>> 2, "-4");
    `CKD(s8 >> 2, "60");               // logical shift: 8'hf0 >> 2 = 8'h3c
    r16 = s8; ck(r16 === 16'hfff0, "sign extension on assignment of signed");
    r16 = v8; ck(r16 === 16'h00f0, "zero extension on assignment of unsigned");
    ck(up[0] === 1'b1 && up[3] === 1'b0 && dn[0] === 1'b0 && dn[3] === 1'b1, "bit selects, [0:3] vs [3:0]");
    ck(up[0:1] === 2'b10 && dn[3:2] === 2'b10, "part selects");
    ck(pk[1] === 4'ha && pk[0] === 4'hb, "packed 2-d");
    // pk3 = 16'h1b2d = 0001 1011 0010 1101; pk3[3] holds the most significant 4 bits
    ck(pk3[3] === 4'b0001 && pk3[2] === 4'b1011 && pk3[1] === 4'b0010 && pk3[0] === 4'b1101, "packed 3-d slices");
    ck(pk3[2][1] === 2'b10, "pk3[2][1]");
    ck(pk3[2][1][0] === 1'b0, "pk3[2][1][0]");
    `CKD(v8[7:4] + 1, "16");           // 32-bit context because of the int literal
    `CKD(v8[3 +: 4], "14");            // bits [6:3] of 8'hf0 = 4'b1110
    `CKD(v8[7 -: 4], "15");
    ck({v8[0], v8[7]} === 2'b01, "concat of bits");
    `CKD($signed(v8), "-16");
    `CKD($unsigned(s8), "240");
    `CKD(s8 < 0, "1");
    `CKD(v8 < 0, "0");
    `CKD(s8 == v8, "1");               // mixed signedness: unsigned compare of identical bits
    `CKD($signed(v8) == s8, "1");
    `CKD(-s8, "16");
    `CKD(s8 * s8, "0");                // 8-bit signed result: 256 wraps to 0
    `CKD(8'sd100 + 8'sd100, "-56");    // 8-bit signed overflow
    `CKD(9'sd100 + 9'sd100, "200");
    `CKD(s8 / 3, "-5");
    `CKD(s8 % 7, "-2");
    `CKD(-8'sd7 / 2, "-3");
    `CKD(-8'sd7 % 3, "-1");
    `CKD(7 % -3, "1");
    `CKD(2 ** 10, "1024");
    `CKD(2 ** -1, "0");
    `CKD((-2) ** 3, "-8");
    `CKD(0 ** 0, "1");
    `CKD(3 ** 3 ** 2, "729");          // ** is left associative: (3**3)**2
    if (errs == 0) $display("PASSED");
  end
endmodule
