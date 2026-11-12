library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package m5v_pkg is
  constant WIDTH : natural := 12;
  subtype byte_t is std_logic_vector(7 downto 0);
  type rec_t is record
    a : bit;
    b : std_logic_vector(3 downto 0);
  end record;
end package;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.m5v_pkg.all;

entity m5v_fields is
  generic (W : positive := 8;
           NAME : string := "x";
           type T;
           package P is new work.m5v_gp generic map (<>);
           function F (x : integer) return integer;
           NODEF : integer);
  port (clk : in std_logic;
        d : in std_logic_vector(7 downto 0);
        q : out std_logic_vector(W - 1 downto 0);
        s : out signed(15 downto 0);
        u : out unsigned(0 to 3);
        w2 : in std_logic_vector(WIDTH - 1 downto 0);
        by : in byte_t;
        flag : in boolean;
        n : out integer range 0 to 15;
        r : in rec_t;
        any : in std_logic_vector;
        b : inout bit;
        bi : buffer std_ulogic);
end entity;

architecture rtl of m5v_fields is
  component m5v_comp is
    port (x : in bit);
  end component;
begin
  uc: m5v_comp port map (x => b);
  ue: entity work.m5v_fields generic map (NODEF => 1) port map (clk, d, q, s, u, w2, by, flag, n, r, any, b, bi);
end architecture;
