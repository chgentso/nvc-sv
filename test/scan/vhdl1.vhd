library ieee, util;
use ieee.std_logic_1164.all;
use util.helpers.all;

entity top is
    port (clk : in std_logic);
end entity;

architecture rtl of top is
    signal s : std_logic;
begin
end architecture;

package mypkg is
    constant k : integer := 1;
end package;

package body mypkg is
end package body;

configuration cfg of top is
    for rtl end for;
end configuration;

context myctx is
    library util;
    use util.other.all;
end context;
