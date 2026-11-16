library ieee;
use ieee.std_logic_1164.all;

entity ng_vh is
end entity;

architecture test of ng_vh is
    component ng_vm is
        generic (W : integer := 4;
                 K : std_logic_vector(7 downto 0) := x"12";
                 N : integer := -3);
        port (clk : in std_logic; q : out std_logic_vector(W - 1 downto 0));
    end component;

    signal clk : std_logic := '0';
    signal q : std_logic_vector(5 downto 0);
begin
    u: ng_vm generic map (W => 6, N => 7) port map (clk, q);
end architecture;
