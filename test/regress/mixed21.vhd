library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity genvec is
    generic (M : std_logic_vector(7 downto 0) := x"00";
             U : std_logic_vector := "1010";
             I : std_logic_vector(3 downto 0) := "0000");
    port (ym : out std_logic_vector(7 downto 0);
          yu : out std_logic_vector(7 downto 0);
          yl : out std_logic_vector(7 downto 0);
          yi : out std_logic_vector(3 downto 0));
end entity;

architecture test of genvec is
begin
    ym <= M;
    yu <= std_logic_vector(resize(unsigned(U), 8));
    yl <= std_logic_vector(to_unsigned(U'length, 8));
    yi <= I;
end architecture;
