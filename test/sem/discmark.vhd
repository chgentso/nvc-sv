package discmark_pkg is
    function res (v : bit_vector) return bit;
    subtype rbit is res bit;
    type rbit_vector is array (natural range <>) of rbit;
end package;

-------------------------------------------------------------------------------

use work.discmark_pkg.all;

entity discmark is
end entity;

architecture test of discmark is
    signal g : rbit bus;
    signal v : rbit_vector(0 to 1) bus;
    signal h : rbit bus;
    disconnect g : bit after 1 ns;              -- Error
    disconnect v : rbit_vector after 1 ns;      -- OK
    disconnect h : rbit after 1 ns;             -- OK
begin
end architecture;
