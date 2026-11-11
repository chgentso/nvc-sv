library ieee;
use ieee.std_logic_1164.all;

entity wn3_ent is
    port ( a : in std_logic_vector(1 downto 0);
           b : out std_logic_vector(1 downto 0) );
end entity;

architecture rtl of wn3_ent is
    signal t : std_logic_vector(1 downto 0);
begin
    t <= not a;
    b <= t;
end architecture;
