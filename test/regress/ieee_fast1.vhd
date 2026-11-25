--
--  Copyright (C) 2026  Nick Gasson
--
--  This program is free software: you can redistribute it and/or modify
--  it under the terms of the GNU General Public License as published by
--  the Free Software Foundation, either version 3 of the License, or
--  (at your option) any later version.
--

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity ieee_fast1 is
end entity;

architecture test of ieee_fast1 is
   type width_array_t is array (natural range <>) of natural;
   constant WIDTHS : width_array_t := (1, 2, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 128);
   constant ALL9   : std_ulogic_vector(0 to 8) := "UX01ZWLH-";

   function make_pattern(w : natural; seed : natural) return std_ulogic_vector is
      variable res : std_ulogic_vector(w - 1 downto 0);
      variable s   : natural := seed + 1;
   begin
      for i in 0 to w - 1 loop
         s := (s * 25173 + 13849) mod 65536;
         if (s / 256) mod 2 = 1 then
            res(i) := '1';
         else
            res(i) := '0';
         end if;
      end loop;
      return res;
   end function;

   function ref_all_01(v : std_ulogic_vector) return boolean is
   begin
      for i in v'range loop
         if v(i) /= '0' and v(i) /= '1' then
            return false;
         end if;
      end loop;
      return true;
   end function;

   procedure test_width(w : natural) is
      variable va, vb, vc : std_ulogic_vector(w - 1 downto 0);
      variable ua, ub, uc, ud : unsigned(w - 1 downto 0);
      variable sa, sb, sc, sd : signed(w - 1 downto 0);
      variable zeros : std_ulogic_vector(w - 1 downto 0) := (others => '0');
      variable ones  : std_ulogic_vector(w - 1 downto 0) := (others => '1');
      variable one_u : unsigned(w - 1 downto 0) := (others => '0');
      variable one_s : signed(w - 1 downto 0) := (others => '0');
      variable nval  : natural;
      variable ival  : integer;
      variable prod_u : unsigned(2 * w - 1 downto 0);
      variable prod_s : signed(2 * w - 1 downto 0);
   begin
      one_u(0) := '1';
      one_s(0) := '1';
      va := make_pattern(w, w * 7 + 3);
      vb := make_pattern(w, w * 13 + 5);
      ua := unsigned(va);
      ub := unsigned(vb);
      sa := signed(va);
      sb := signed(vb);

      -- Arithmetic: + and - identities, carry/borrow wraparound
      uc := (ua + ub) - ub;
      assert uc = ua report "unsigned (a+b)-b mismatch w=" & integer'image(w);
      uc := (ua - ub) + ub;
      assert uc = ua report "unsigned (a-b)+b mismatch w=" & integer'image(w);
      assert unsigned(ones) + unsigned(one_u) = unsigned(zeros)
         report "unsigned wrap +1 w=" & integer'image(w);
      assert unsigned(zeros) - unsigned(one_u) = unsigned(ones)
         report "unsigned wrap -1 w=" & integer'image(w);

      sc := (sa + sb) - sb;
      assert sc = sa report "signed (a+b)-b mismatch w=" & integer'image(w);
      sc := (sa - sb) + sb;
      assert sc = sa report "signed (a-b)+b mismatch w=" & integer'image(w);
      assert (-sa) + sa = signed(zeros)
         report "signed (-a)+a mismatch w=" & integer'image(w);
      assert (unsigned(zeros) + 0) = unsigned(zeros);
      assert (0 + unsigned(zeros)) = unsigned(zeros);
      assert (unsigned(zeros) + '1') = one_u;
      assert ('1' + unsigned(zeros)) = one_u;
      assert (one_u - '1') = unsigned(zeros);
      assert ('1' - one_u) = unsigned(zeros);
      assert (signed(zeros) + 0) = signed(zeros);
      assert (0 + signed(zeros)) = signed(zeros);
      assert (signed(zeros) - 0) = signed(zeros);
      assert (0 - signed(zeros)) = signed(zeros);
      assert (signed(zeros) + '1') = one_s;
      assert ('1' + signed(zeros)) = one_s;
      assert (one_s - '1') = signed(zeros);
      assert ('1' - one_s) = signed(zeros);

      if w >= 2 then
         assert (ua + 1) - 1 = ua;
         assert (1 + ua) - 1 = ua;
         assert (sa + 1) - 1 = sa;
         assert (1 + sa) - 1 = sa;
         assert (sa + (-1)) = sa - 1;
         assert ((-1) + sa) = sa - 1;
         assert (0 - sa) = -sa;
         if sa(w - 1) = '0' then
            assert abs(sa) = sa;
         else
            assert abs(sa) = -sa;
         end if;
      end if;

      -- Multiply & divide sanity
      prod_u := ua * one_u;
      assert prod_u(w - 1 downto 0) = ua;
      assert prod_u(2 * w - 1 downto w) = unsigned(zeros);
      if w >= 2 then
         prod_s := sa * one_s;
         assert prod_s(w - 1 downto 0) = sa;
         assert prod_s = resize(sa, 2 * w);
      end if;
      assert (ua / one_u) = ua;
      assert (ua rem one_u) = unsigned(zeros);

      -- Comparisons (self, ones vs zeros, signed negative vs positive)
      assert ua = ua and not (ua /= ua);
      assert not (ua < ua) and ua <= ua and not (ua > ua) and ua >= ua;
      assert sa = sa and not (sa /= sa);
      assert not (sa < sa) and sa <= sa and not (sa > sa) and sa >= sa;
      assert unsigned(zeros) < unsigned(ones);
      assert unsigned(ones) > unsigned(zeros);
      assert unsigned(zeros) <= unsigned(ones);
      assert unsigned(ones) >= unsigned(zeros);
      assert unsigned(zeros) /= unsigned(ones);
      assert unsigned(zeros) = 0 and 0 = unsigned(zeros);
      assert unsigned(ones) /= 0 and 0 /= unsigned(ones);
      assert unsigned(ones) > 0 and 0 < unsigned(ones);
      assert unsigned(ones) >= 1 and 1 <= unsigned(ones);
      assert signed(ones) = -1 and -1 = signed(ones);
      assert signed(ones) < 0 and 0 > signed(ones);
      assert signed(ones) < signed(zeros) and signed(zeros) > signed(ones);
      assert signed(ones) <= signed(zeros) and signed(zeros) >= signed(ones);

      -- byte_vector_equal at every bit position (MSB, mid, LSB)
      assert va = va;
      vc := va;
      vc(w - 1) := not vc(w - 1);
      assert vc /= va;
      vc := va;
      vc(w / 2) := not vc(w / 2);
      assert vc /= va;
      vc := va;
      vc(0) := not vc(0);
      assert vc /= va;

      -- Conversions: to_integer / to_unsigned / to_signed / resize
      if w <= 30 then
         nval := to_integer(ua);
         assert to_unsigned(nval, w) = ua;
         assert to_unsigned(nval, ua) = ua;
         assert ua = nval and nval = ua;
         assert not (ua < nval) and not (ua > nval) and ua <= nval and ua >= nval;
         ival := to_integer(sa);
         assert to_signed(ival, w) = sa;
         assert to_signed(ival, sa) = sa;
         assert sa = ival and ival = sa;
         assert not (sa < ival) and not (sa > ival) and sa <= ival and sa >= ival;
      else
         assert to_integer(unsigned(zeros)) = 0;
         assert to_integer(one_u) = 1;
         assert to_integer(signed(zeros)) = 0;
         assert to_integer(signed(ones)) = -1;
         assert to_unsigned(12345, w) = resize(to_unsigned(12345, 16), w);
         assert to_signed(-12345, w) = resize(to_signed(-12345, 16), w);
         assert to_integer(to_signed(-12345, w)) = -12345;
         assert to_integer(to_unsigned(12345, w)) = 12345;
      end if;

      -- Bitwise vector-vector, vector-scalar, not, and unary reductions
      assert (va and vb) = not (va nand vb);
      assert (va or vb) = not (va nor vb);
      assert (va xor vb) = not (va xnor vb);
      assert (not (not va)) = va;
      assert (ua and ub) = unsigned(va and vb);
      assert (ua or ub) = unsigned(va or vb);
      assert (ua xor ub) = unsigned(va xor vb);
      assert (ua nand ub) = unsigned(va nand vb);
      assert (ua nor ub) = unsigned(va nor vb);
      assert (ua xnor ub) = unsigned(va xnor vb);
      assert (not ua) = unsigned(not va);
      assert (sa xor sb) = signed(va xor vb);
      assert (va and '1') = va and ('1' and va) = va;
      assert (va and '0') = zeros and ('0' and va) = zeros;
      assert (va or '0') = va and ('0' or va) = va;
      assert (va or '1') = ones and ('1' or va) = ones;
      assert (va xor '1') = not va and ('1' xor va) = not va;
      assert (va nand '1') = not va and ('1' nand va) = not va;
      assert (va nor '0') = not va and ('0' nor va) = not va;
      assert (va xnor '0') = not va and ('0' xnor va) = not va;
      assert (ua and '1') = ua and ('1' and ua) = ua;
      assert (sa or '0') = sa and ('0' or sa) = sa;

      assert (or zeros) = '0' and (nor zeros) = '1';
      assert (or ones) = '1' and (nor ones) = '0';
      assert (and ones) = '1' and (nand ones) = '0';
      assert (and zeros) = '0' and (nand zeros) = '1';
      assert (or std_ulogic_vector(one_u)) = '1';
      assert (or one_u) = '1';
      if w = 1 then
         assert (and one_u) = '1';
      else
         assert (and one_u) = '0';
      end if;
      assert (xor zeros) = '0' and (xnor zeros) = '1';
      assert (xor std_ulogic_vector(one_u)) = '1';
      assert (xnor std_ulogic_vector(one_u)) = '0';
      if w mod 2 = 0 then
         assert (xor ones) = '0' and (xnor ones) = '1';
      else
         assert (xor ones) = '1' and (xnor ones) = '0';
      end if;

      -- is_x, to_01, to_x01, to_x01z, to_ux01
      assert not is_x(va) and not is_x(ua) and not is_x(sa);
      assert to_01(va) = va and to_01(ua) = ua and to_01(sa) = sa;
      assert to_x01(va) = va and to_x01z(va) = va and to_ux01(va) = va;
      vc := va;
      vc(0) := 'X';
      assert is_x(vc) and is_x(unsigned(vc)) and is_x(signed(vc));
      vc := va;
      vc(w - 1) := 'U';
      assert is_x(vc);
      vc := va;
      vc(w / 2) := 'Z';
      assert is_x(vc);
      vc := va;
      vc(0) := 'L';
      vc(w - 1) := 'H';
      assert not is_x(vc);

      -- Shifts and rotates
      assert shift_left(ua, 0) = ua and shift_right(ua, 0) = ua;
      assert shift_left(ua, w) = unsigned(zeros) and shift_right(ua, w) = unsigned(zeros);
      assert shift_left(ua, w + 5) = unsigned(zeros);
      assert rotate_left(rotate_right(ua, 1), 1) = ua;
      assert rotate_left(ua, w) = ua and rotate_right(ua, w) = ua;
      assert (ua sll 1) = shift_left(ua, 1) and (ua sll (-1)) = shift_right(ua, 1);
      assert (ua srl 1) = shift_right(ua, 1) and (ua srl (-1)) = shift_left(ua, 1);
      assert (ua rol 3) = rotate_left(ua, 3) and (ua ror 3) = rotate_right(ua, 3);
      assert (sa sra w) = (w - 1 downto 0 => sa(w - 1));
      assert (va sll 0) = va and (va srl 0) = va;
      assert ((va rol 1) ror 1) = va;

      -- std_match and ?= / ?/=
      assert std_match(va, va) and std_match(ua, ua) and std_match(sa, sa);
      assert (va ?= va) = '1' and (va ?/= va) = '0';
      assert (ua ?= ua) = '1' and (ua ?/= ua) = '0';
      assert (sa ?= sa) = '1' and (sa ?/= sa) = '0';
      vc := va;
      vc(0) := '-';
      assert std_match(va, vc) and (va ?= vc) = '1' and (va ?/= vc) = '0';
      assert (ua ?= unsigned(vc)) = '1' and (ua ?/= unsigned(vc)) = '0';

      -- to_hstring / to_ostring sanity
      assert to_hstring(zeros)'length = (w + 3) / 4;
      assert to_hstring(unsigned(zeros)) = to_hstring(zeros);
      assert to_ostring(zeros)'length = (w + 2) / 3;
   end procedure;

begin
   tb: process is
      variable nu1, nu2 : unsigned(0 downto 1);
      variable ns1, ns2 : signed(0 downto 1);
      variable nv1      : std_ulogic_vector(1 to 0);
      variable u64a, u64b : unsigned(63 downto 0);
      variable s64a, s64b : signed(63 downto 0);
      variable u128     : unsigned(127 downto 0);
      variable s128     : signed(127 downto 0);
      variable u65      : unsigned(64 downto 0);
      variable s65      : signed(64 downto 0);
      variable m9       : std_ulogic_vector(0 to 8) := ALL9;
      variable r1, r2   : std_ulogic_vector(8 downto 0);
      variable tmp_b    : boolean;
      variable tmp_u    : std_ulogic;
      variable v_255    : natural := 255;
      variable v_127    : integer := 127;
      variable v_ux4    : unsigned(3 downto 0) := "01X1";
      variable v_u04    : unsigned(3 downto 0) := "0101";
      variable v_su4    : signed(3 downto 0) := "01U1";
      variable v_s04    : signed(3 downto 0) := "0101";
      variable v_u1     : unsigned(3 downto 0) := "0001";
      variable v_s1     : signed(3 downto 0) := "0001";
      variable v_u2     : unsigned(1 downto 0) := "01";
      variable v_u3     : unsigned(2 downto 0) := "010";
   begin
      -- 1. All edge widths 1..128
      for idx in WIDTHS'range loop
         test_width(WIDTHS(idx));
      end loop;

      -- 2. Null vectors (width 0)
      assert unsigned'(nu1 + nu2)'length = 0;
      assert unsigned'(nu1 - nu2)'length = 0;
      assert unsigned'(nu1 * nu2)'length = 0;
      assert unsigned'(nu1 + 1)'length = 0;
      assert unsigned'(1 + nu1)'length = 0;
      assert unsigned'(nu1 - 1)'length = 0;
      assert unsigned'(1 - nu1)'length = 0;
      assert signed'(ns1 + ns2)'length = 0;
      assert signed'(ns1 - ns2)'length = 0;
      assert signed'(-ns1)'length = 0;
      assert signed'(abs ns1)'length = 0;
      assert to_unsigned(5, 0)'length = 0;
      assert to_signed(-5, 0)'length = 0;
      assert resize(nu1, 0)'length = 0;
      assert resize(ns1, 0)'length = 0;
      assert resize(nu1, 8) = x"00";
      assert resize(ns1, 8) = x"00";
      assert shift_left(nu1, 3)'length = 0;
      assert shift_right(ns1, 3)'length = 0;
      assert rotate_left(nu1, 3)'length = 0;
      assert rotate_right(ns1, 3)'length = 0;
      assert (or nv1) = '0' and (nor nv1) = '1';
      assert (and nv1) = '1' and (nand nv1) = '0';
      assert (xor nv1) = '0' and (xnor nv1) = '1';
      assert not is_x(nv1);
      assert nv1 = nv1;
      assert (nv1 ?= nv1) = '1' and (nv1 ?/= nv1) = '0';
      assert to_01(nv1)'length = 0;

      -- 3. 64-bit & 128-bit exact arithmetic (including 64x64->128 multiply)
      u64a := x"123456789ABCDEF0";
      u64b := x"FEDCBA9876543210";
      assert u64a + u64b = x"1111111111111100";
      assert u64b - u64a = x"ECA8641FDB975320";
      u128 := unsigned'(x"0000000100000000") * unsigned'(x"0000000100000000");
      assert u128 = x"00000000000000010000000000000000";
      u128 := unsigned'(x"FFFFFFFFFFFFFFFF") * unsigned'(x"FFFFFFFFFFFFFFFF");
      assert u128 = x"FFFFFFFFFFFFFFFE0000000000000001";
      s64a := signed'(x"FFFFFFFFFFFFFFFF"); -- -1
      s64b := signed'(x"8000000000000000"); -- -2^63
      s128 := s64a * s64b;
      assert s128 = signed'(x"00000000000000008000000000000000");
      assert s64b < s64a and s64a > s64b;
      assert s64b < 0 and 0 > s64b;

      -- 4. Mixed-width arithmetic and comparisons (including > 64 bits)
      assert unsigned'("1111") + unsigned'("00000001") = x"10";
      assert signed'("1111") + signed'("00000001") = x"00"; -- -1 + 1 = 0
      assert signed'("1111") = signed'("11111111");
      assert signed'("1111") < signed'("00000000");
      assert unsigned'("0010") < 1000;
      assert 1000 > unsigned'("0010");
      assert unsigned'("0010") /= 1000;
      assert signed'("0111") < 1000 and 1000 > signed'("0111");
      assert signed'("1000") > (-1000) and (-1000) < signed'("1000");
      u65 := '1' & x"0000000000000000";
      assert u65 > 1000000 and 1000000 < u65 and u65 /= 0;
      s65 := '1' & x"0000000000000000";
      assert s65 < (-1000000) and (-1000000) > s65;
      s65 := '0' & x"8000000000000000";
      assert s65 > 1000000 and 1000000 < s65;

      -- 5. All 9-state semantics (metavalues U, X, Z, W, L, H, -)
      assert to_x01(m9)  = "XX01XX01X";
      assert to_x01z(m9) = "XX01ZX01X";
      assert to_ux01(m9) = "UX01XX01X";
      assert to_01(std_ulogic_vector'("01LH01LH"), 'X') = "01010101";
      assert to_01(std_ulogic_vector'("01LHU1LH"), 'X') = "XXXXXXXX";
      assert to_01(std_ulogic_vector'("01LHZ1LH"), '0') = "00000000";
      assert to_01(unsigned'("LH01"), 'X') = "0101";
      assert to_01(signed'("HL10"), 'X') = "1010";
      assert std_ulogic_vector(to_01(signed'("HUX0"), 'X')) = "XXXX";
      assert is_x('U') and is_x('X') and is_x('Z') and is_x('W') and is_x('-');
      assert not is_x('0') and not is_x('1') and not is_x('L') and not is_x('H');

      -- 9-state unary reductions priority: 1/H > U > X/Z/W/- > 0/L for OR
      assert (or std_ulogic_vector'("0L0L")) = '0';
      assert (or std_ulogic_vector'("0LX0")) = 'X';
      assert (or std_ulogic_vector'("0LXU")) = 'U';
      assert (or std_ulogic_vector'("UX1Z")) = '1';
      assert (or std_ulogic_vector'("UXHZ")) = '1';
      assert (nor std_ulogic_vector'("0LXU")) = 'U';
      assert (nor std_ulogic_vector'("UXHZ")) = '0';
      -- AND priority: 0/L > U > X/Z/W/- > 1/H
      assert (and std_ulogic_vector'("1H1H")) = '1';
      assert (and std_ulogic_vector'("1HX1")) = 'X';
      assert (and std_ulogic_vector'("1HXU")) = 'U';
      assert (and std_ulogic_vector'("UX0Z")) = '0';
      assert (and std_ulogic_vector'("UXLZ")) = '0';
      assert (nand std_ulogic_vector'("UXLZ")) = '1';
      -- XOR priority: U > X/Z/W/- > parity(1/H)
      assert (xor std_ulogic_vector'("HL01")) = '0';
      assert (xor std_ulogic_vector'("HL11")) = '1';
      assert (xor std_ulogic_vector'("1X01")) = 'X';
      assert (xor std_ulogic_vector'("1XU1")) = 'U';
      assert (xnor std_ulogic_vector'("1XU1")) = 'U';

      -- 9-state arithmetic (L/H treated as 0/1; U/X/Z/W/- yields all 'X')
      assert unsigned'("HLHL") + unsigned'("0001") = "1011";
      assert std_ulogic_vector(unsigned'("10X1") + unsigned'("0001")) = "XXXX";
      assert std_ulogic_vector(signed'("10U1") - signed'("0001")) = "XXXX";
      assert std_ulogic_vector(-signed'("HL01")) = "0111"; -- -(-7) = +7
      assert std_ulogic_vector(-signed'("HL00")) = "1000"; -- -(-8) = -8 in 4-bit
      assert std_ulogic_vector(abs(signed'("HL01"))) = "0111";
      assert std_ulogic_vector(-signed'("10Z1")) = "XXXX";
      assert to_integer(unsigned'("HL01")) = 9;
      assert to_integer(signed'("HL01")) = -7;

      -- 9-state TO_HSTRING / TO_OSTRING
      assert to_hstring(std_ulogic_vector'("10100101")) = "A5";
      assert to_hstring(std_ulogic_vector'("10101")) = "15";
      assert to_hstring(std_ulogic_vector'("Z0101")) = "Z5";
      assert to_hstring(std_ulogic_vector'("ZZZZ0101")) = "Z5";
      assert to_hstring(std_ulogic_vector'("ZZZ00101")) = "X5";
      assert to_hstring(std_ulogic_vector'("HL01")) = "9";
      assert to_hstring(signed'("10101")) = "F5";
      assert to_hstring(unsigned'("10101")) = "15";
      assert to_ostring(std_ulogic_vector'("10101")) = "25";
      assert to_ostring(signed'("10101")) = "65";

      -- 9-state STD_MATCH and ?= / ?/=
      assert std_match(std_ulogic_vector'("HL01-"), std_ulogic_vector'("10LHU"));
      assert not std_match(std_ulogic_vector'("01X1"), std_ulogic_vector'("01X1"));
      assert (std_ulogic_vector'("01-1") ?= std_ulogic_vector'("01X1")) = '1';
      assert (std_ulogic_vector'("01U1") ?= std_ulogic_vector'("0001")) = '0';
      assert (std_ulogic_vector'("01U1") ?= std_ulogic_vector'("0101")) = 'U';
      assert (unsigned'("01U1") ?= unsigned'("0001")) = 'U'; -- NUMERIC_STD ?= returns U immediately
      assert (unsigned'("01X1") ?= unsigned'("0001")) = 'X';
      assert (unsigned'("01-1") ?= unsigned'("01X1")) = '1';
      assert (unsigned'("01-1") ?/= unsigned'("01X1")) = '0';

      -- Exercise warning paths at t > 0 (metavalues / null / truncation) to verify exact fallback
      wait for 1 ns;
      tmp_b := (v_ux4 < v_u04);
      assert not tmp_b;
      tmp_b := (v_ux4 /= v_u04);
      assert tmp_b;
      tmp_b := (v_su4 >= v_s04);
      assert not tmp_b;
      tmp_b := (nu1 = nu2);
      assert not tmp_b;
      tmp_b := (nu1 /= nu2);
      assert tmp_b;
      tmp_u := (nu1 ?= nu2);
      assert tmp_u = 'X';
      assert to_integer(nu1) = 0;
      assert to_integer(ns1) = 0;
      assert to_integer(v_ux4) = 0;
      assert to_integer(v_su4) = 0;
      assert to_unsigned(v_255, 4) = "1111";
      assert to_signed(v_127, 4) = "1111";
      assert (v_u1 + v_255) = "0000";
      assert (v_s1 - v_127) = "0010";
      tmp_b := std_match(v_u2, v_u3);
      assert not tmp_b;
      tmp_b := std_match(nu1, nu2);
      assert not tmp_b;

      -- 9-state bitwise check across ALL 9 states
      r1 := m9 and "010101010";
      r2 := m9 nand "010101010";
      assert r1 = not r2;
      r1 := m9 or "010101010";
      r2 := m9 nor "010101010";
      assert r1 = not r2;
      r1 := m9 xor "010101010";
      r2 := m9 xnor "010101010";
      assert r1 = not r2;

      report "ieee_fast1 PASSED";
      wait;
   end process;
end architecture;
