library ieee;
use ieee.std_logic_1164.all;

entity sub is
    port (x : out std_logic_vector(3 downto 0);
          y : in std_logic_vector(3 downto 0));
end entity;

architecture a of sub is
begin
    x <= "1010" when y(0) = '1' else "0101";
end architecture;

-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

entity compopen1 is
end entity;

architecture test of compopen1 is
    function twice (n : natural) return natural is
    begin
        return n * 2;
    end function;

    component sub is
        port (x : out std_logic_vector(twice(4) - 1 downto 0);
              y : in std_logic_vector(3 downto 0));
    end component;

    signal i4 : std_logic_vector(3 downto 0) := x"1";
begin

    u1: component sub
        port map ( x => open, y => i4 );

    process is
    begin
        wait for 1 ns;
        assert i4 = x"1";
        wait;
    end process;

end architecture;
