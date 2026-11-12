entity sub1 is
    port (a : in bit; y : out bit);
end entity;

architecture test of sub1 is
begin
    y <= a;
end architecture;

entity unused1 is
    port (a, b : in bit;
          q : out bit);
end entity;

architecture test of unused1 is
    component sub1 is
        port (a : in bit; y : out bit);
    end component;

    signal s_unused : bit;             -- never read
    signal s_undriven : bit;           -- never assigned
    signal s_ok : bit;
    signal s_init : bit := '1';        -- initial value
    signal s_inst : bit;               -- driven by an instance
    signal s_off : bit;                -- nvc lint_off unused
    signal s_vec : bit_vector(1 downto 0);
begin
    u1: component sub1 port map (a => a, y => s_inst);
    s_ok <= a and s_inst;
    s_unused <= s_ok;
    s_off <= s_ok;
    s_vec(0) <= a;
    s_vec(1) <= b;
    q <= s_undriven and s_ok and s_init and s_vec(0) and s_vec(1);
end architecture;
