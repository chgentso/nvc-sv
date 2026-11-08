package shim1_pkg is
    type rec_t is record
        x : bit;
    end record;
    impure function now_ns return integer;
end package;

package body shim1_pkg is
    impure function now_ns return integer is
    begin
        return integer(now / 1 ns);
    end function;
end package body;

-------------------------------------------------------------------------------

entity mode1 is
    port ( a : linkage bit );
end entity;

architecture test of mode1 is
begin
end architecture;

-------------------------------------------------------------------------------

entity uncon1 is
    port ( a : in bit_vector );
end entity;

architecture test of uncon1 is
begin
end architecture;

-------------------------------------------------------------------------------

entity type1 is
    generic ( type t );
    port ( a : in bit );
end entity;

architecture test of type1 is
begin
end architecture;

-------------------------------------------------------------------------------

entity gentype1 is
    generic ( t : time := 1 ns );
    port ( a : in bit );
end entity;

architecture test of gentype1 is
begin
end architecture;

-------------------------------------------------------------------------------

use work.shim1_pkg.all;

entity rec1 is
    port ( a : in rec_t );
end entity;

architecture test of rec1 is
begin
end architecture;

-------------------------------------------------------------------------------

entity real1 is
    generic ( g : real := 4.0 );
    port ( a : in bit_vector(integer(g) - 1 downto 0) );
end entity;

architecture test of real1 is
begin
end architecture;

-------------------------------------------------------------------------------

entity null1 is
    generic ( w : natural := 0 );
    port ( a : in bit_vector(w - 1 downto 0) );
end entity;

architecture test of null1 is
begin
end architecture;

-------------------------------------------------------------------------------

entity null2 is
    generic ( w : natural := 0;
              v : bit_vector(w - 1 downto 0) := "" );
    port ( a : in bit );
end entity;

architecture test of null2 is
begin
end architecture;

-------------------------------------------------------------------------------

entity ovf1 is
    generic ( g : integer := 100 );
    port ( a : in bit_vector(g ** g downto 0) );
end entity;

architecture test of ovf1 is
begin
end architecture;

-------------------------------------------------------------------------------

use work.shim1_pkg.all;

entity def1 is
    generic ( g : integer := now_ns );
    port ( a : in bit );
end entity;

architecture test of def1 is
begin
end architecture;

-------------------------------------------------------------------------------

entity ext1 is
    port ( \a b\ : in bit );
end entity;

architecture test of ext1 is
begin
end architecture;
