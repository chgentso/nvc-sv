entity attr29 is
end entity;

architecture test of attr29 is
    type pair_t is record
        a : natural;
        b : natural;
    end record;
    type pair_vec_t is array (natural range <>) of pair_t;

    attribute pairs : pair_vec_t;
    attribute pairs of pair_t : type is ((1, 10), (2, 20), (3, 30));

    signal sums : integer_vector(0 to 2);
begin
    g : for i in pair_t'pairs'range generate
        constant ca : natural := pair_t'pairs(i).a;
        constant cb : natural := pair_t'pairs(i).b;
    begin
        sums(i) <= ca + cb;
    end generate;

    check : process is
    begin
        wait for 1 ns;
        assert sums(0) = 11;
        assert sums(1) = 22;
        assert sums(2) = 33;
        wait;
    end process;
end architecture;
