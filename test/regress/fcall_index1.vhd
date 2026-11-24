-- Chained indexing on a function callable with no arguments (all parameters
-- defaulted) returning an array of arrays: F(i, j)(k) must resolve as
-- F()(i, j)(k) and G(i)(k) as G()(i)(k) when context requires the inner
-- element type.

entity fcall_index1 is
end entity;

architecture test of fcall_index1 is
    type grid_t is array (natural range <>, natural range <>) of integer_vector;
    type rows_t is array (natural range <>) of integer_vector;

    function make_grid (a, b : integer := 7) return grid_t is
    begin
        return (0 to 2 => (0 to 1 => (0 to 3 => a + b)));
    end function;

    function make_rows (a : integer := 11) return rows_t is
    begin
        return (0 to 2 => (0 to 3 => a * 3));
    end function;

    constant c1 : integer := make_grid(2, 1)(3);
    constant c2 : integer := make_rows(1)(2);
    constant c3 : integer_vector(0 to 3) := make_rows(5)(1);
begin
    process is
    begin
        assert c1 = 14 report integer'image(c1);
        assert c2 = 33 report integer'image(c2);
        assert c3(0) = 15 report integer'image(c3(0));
        wait;
    end process;
end architecture;
