library ieee;
use ieee.std_logic_1164.all;

entity vhdl_island_subprog_refs1 is
end entity;

architecture test of vhdl_island_subprog_refs1 is
   signal a, b, sel : std_logic := '0';
   signal s_mid1, s_mid2, y : std_logic := '0';
   signal ext_drv : std_logic := '0';
   signal obs_ext : std_logic := '0';

   function pick_sel(s, x0, x1 : std_logic) return std_logic is
   begin
      if s = '1' then
         return x1;
      else
         return x0;
      end if;
   end function;

   procedure drive_sig(signal dst : out std_logic; constant v : in std_logic) is
   begin
      dst <= v;
   end procedure;
begin
   -- Three-process island: s_mid1 and s_mid2 are only read externally inside
   -- a local procedure declared in the unfused checker process below.
   s_mid1 <= a;
   s_mid2 <= b;
   y <= pick_sel(sel, s_mid1, s_mid2);

   -- Concurrent assignment reading a signal driven via procedure out-param
   obs_ext <= not ext_drv;

   checker : process
      variable got1, got2 : std_logic;

      procedure sample_mid(variable o1, o2 : out std_logic) is
      begin
         o1 := s_mid1;
         o2 := s_mid2;
      end procedure;
   begin
      a <= '1';
      b <= '0';
      sel <= '0';
      drive_sig(ext_drv, '1');
      wait for 10 ns;

      sample_mid(got1, got2);
      assert got1 = '1' report "expected s_mid1='1', got " & std_logic'image(got1);
      assert got2 = '0' report "expected s_mid2='0', got " & std_logic'image(got2);
      assert y = '1' report "expected y='1'";
      assert obs_ext = '0' report "expected obs_ext='0'";

      a <= '0';
      b <= '1';
      sel <= '1';
      drive_sig(ext_drv, '0');
      wait for 10 ns;

      sample_mid(got1, got2);
      assert got1 = '0' report "expected s_mid1='0', got " & std_logic'image(got1);
      assert got2 = '1' report "expected s_mid2='1', got " & std_logic'image(got2);
      assert y = '1' report "expected y='1'";
      assert obs_ext = '1' report "expected obs_ext='1'";
      wait;
   end process;
end architecture;
