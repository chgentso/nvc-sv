library ieee;
use ieee.std_logic_1164.all;

entity mixed28_inner is
    generic (W : positive := 2);
    port (a : in std_logic_vector(W - 1 downto 0);
          y : out std_logic_vector(W - 1 downto 0));
end entity;

architecture test of mixed28_inner is
    component mixed28_leaf is
        port (i : in std_logic;
              o : out std_logic);
    end component;
    signal count : natural := 0;
begin

    g: for i in 0 to W - 1 generate
        u: component mixed28_leaf port map (a(i), y(i));
    end generate;

    process (a'transaction) is
    begin
        count <= count + 1;
    end process;

end architecture;

-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

entity mixed28 is
end entity;

architecture test of mixed28 is
    component mixed28_mid is
        port (a : in std_logic_vector(3 downto 0);
              y : out std_logic_vector(3 downto 0));
    end component;
    signal a, y : std_logic_vector(3 downto 0);
begin

    u: component mixed28_mid port map (a, y);

    process is
    begin
        a <= X"5";
        wait for 1 ns;
        assert y = X"a";
        a <= X"3";
        wait for 1 ns;
        assert y = X"c";
        wait;
    end process;

end architecture;
