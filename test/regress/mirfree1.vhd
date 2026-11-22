entity mirfree1 is
end entity;

architecture test of mirfree1 is
   signal s : integer := 0;
begin

   p1: process is
      variable v : integer := 5;
      impure function add_v (x : integer) return integer is
      begin
         return x + v;
      end function;
      impure function twice_v (x : integer) return integer is
      begin
         return x + 2 * v;
      end function;
      procedure bump is
      begin
         v := v + 1;
      end procedure;
   begin
      assert add_v(1) = 6;
      wait for 1 ns;
      bump;
      wait for 1 ns;
      assert twice_v(1) = 13;
      s <= add_v(twice_v(0));
      wait for 1 ns;
      assert s = 18;
      report "PASSED";
      wait;
   end process;

   p2: process is
      variable w : integer := 100;
      impure function get_w return integer is
      begin
         return w;
      end function;
   begin
      wait for 2 ns;
      w := w + s;
      assert get_w = 100;
      wait;
   end process;

end architecture;
