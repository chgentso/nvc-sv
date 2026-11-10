library util;
use work.mypkg.all;
use util.helpers.all;

entity tb is
end entity;

architecture sim of tb is
    component leaf is
        port (a : in bit);
    end component;
    signal x : bit;
begin
    u1: component leaf port map (a => x);
    u2: entity util.core port map (x);
    u3: entity work.other(rtl) port map (x);
    u4: configuration util.cfg2;
end architecture;
