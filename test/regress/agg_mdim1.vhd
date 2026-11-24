-- Multidimensional array aggregate whose element type is an unconstrained
-- array (e.g. 2D array of integer_vector): the inferred aggregate subtype
-- must preserve the inner element bounds rather than duplicating dim 2.

entity agg_mdim1 is
end entity;

architecture test of agg_mdim1 is
    type grid_t is array (natural range <>, natural range <>) of integer_vector;

    function make_grid (v : integer) return grid_t is
    begin
        return (0 to 2 => (0 to 1 => (0 to 5 => v)));
    end function;
begin
    process is
        variable g : grid_t(0 to 2, 0 to 1)(0 to 5);
    begin
        g := make_grid(42);
        assert g'length(1) = 3;
        assert g'length(2) = 2;
        assert g(2, 1)'length = 6;
        assert g(2, 1)(5) = 42;
        wait;
    end process;
end architecture;
