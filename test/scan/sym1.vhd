library ieee;
use ieee.std_logic_1164.all;
use work.mypkg.all;

package mypkg is
  type state_t is (idle, run);
  type rec_t is record
    a : integer;
    b : bit;
  end record;
  function f(x : integer) return integer;
end package;

package body mypkg is
  function f(x : integer) return integer is
    variable y : integer := x;
  begin
    return y + 1;
  end function;
end package body;

entity e is
  generic (N : natural := 4);
  port (clk : in bit; q : out integer);
end entity;

architecture rtl of e is
  signal s : integer;
  signal r : work.mypkg.rec_t;
  component leaf port (c : in bit); end component;
begin
  u1: leaf port map (c => clk);
  p: process (clk)
    variable v : integer;
  begin
    v := f(s);
    s <= v + N;
    q <= r.a;
  end process;
end architecture;
