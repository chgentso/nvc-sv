entity unused2 is
    port ( a : in bit;
           q : out bit );
end entity;

architecture test of unused2 is
    attribute keep : boolean;

    signal s_keep : bit;               -- deliberately kept
    signal s_unused : bit;
    signal s_undriven_keep : bit;
    attribute keep of s_keep : signal is true;
    attribute keep of s_undriven_keep : signal is true;
begin
    s_keep <= a;
    s_unused <= a;
    q <= a;
end architecture;
