package m5v_calls_pkg is
  function f (n : natural) return natural;
end package;

use work.m5v_calls_pkg.all;

entity m5v_calls is
  generic (N : natural := 4);
  port (a : in bit_vector(f(N) - 1 downto 0);
        b : out bit_vector(f(f(N)) - 1 downto 0));
end entity;
