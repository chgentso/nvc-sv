library ieee;
use ieee.std_logic_1164.all;

entity mixed36 is
end entity;

architecture test of mixed36 is
    type mode_t is (CLASSIC, PIPELINED, BURST);

    component mixed36_sub is
        generic (vec  : std_logic_vector(7 downto 0);
                 bv   : bit_vector(3 downto 0);
                 flag : boolean;
                 mode : mode_t);
        port (clk : in std_logic;
              o   : out std_logic_vector(7 downto 0);
              p   : out std_logic_vector(3 downto 0);
              q   : out std_logic;
              r   : out std_logic_vector(1 downto 0));
    end component;

    signal clk : std_logic := '0';
    signal o   : std_logic_vector(7 downto 0);
    signal p   : std_logic_vector(3 downto 0);
    signal q   : std_logic;
    signal r   : std_logic_vector(1 downto 0);
begin
    u: component mixed36_sub
        generic map (vec => x"a5", bv => "1001", flag => true, mode => BURST)
        port map (clk, o, p, q, r);

    check: process is
    begin
        wait for 1 ns;
        assert o = x"a5";
        assert p = "1001";
        assert q = '1';
        assert r = "10";
        wait;
    end process;
end architecture;
