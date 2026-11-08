library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mixed_types is
    generic (W : positive := 4);
    port (u    : in unsigned(W - 1 downto 0);
          s    : in signed(W - 1 downto 0);
          bv   : in bit_vector(W - 1 downto 0);
          b    : in bit;
          n    : in natural;
          ou   : out unsigned(W - 1 downto 0);
          os   : out signed(W - 1 downto 0);
          obv  : out bit_vector(W - 1 downto 0);
          ob   : buffer bit;
          oi   : out integer;
          wide : out std_ulogic_vector(2 ** W - 1 downto 0);
          mm   : out std_ulogic_vector(W mod 3 downto 0));
end entity;

architecture test of mixed_types is
begin
    ou   <= u + 1;
    os   <= s - 1;
    obv  <= not bv;
    ob   <= not b;
    oi   <= n - 10;
    wide <= (others => '1');
    mm   <= (others => '1');
end architecture;
