package mixed58_pkg is
  type family_t is (virtex7, zynq, artix7);
end package;

use work.mixed58_pkg.all;

entity mixed58_e is
  generic (C_FAMILY : family_t := zynq;
           C_BOOL : boolean := false;
           C_NAME : string := "none";
           C_EXPECT : string);
  port (a : in bit);
end entity;

architecture test of mixed58_e is
begin
  process is
  begin
    wait for 1 ns;
    assert family_t'image(C_FAMILY) & " " & boolean'image(C_BOOL) & " " & C_NAME
      = C_EXPECT report "unexpected generics" severity failure;
    wait;
  end process;
end architecture;
