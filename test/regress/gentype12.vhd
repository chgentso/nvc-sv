-- Predefined function attributes associated with formal generic functions
-- (LRM 6.5.7.2).

package gentype12_pkg is
    generic (
        function to_img(x : integer) return string;
        function next_v(x : integer) return integer;
        function prev_v(x : integer) return integer;
        function to_pos(x : character) return integer;
        function to_val(x : integer) return character;
        function right_v(x : integer) return integer;
        function left_v(x : integer) return integer
    );
    function eval_all return string;
end package;

package body gentype12_pkg is
    function eval_all return string is
    begin
        return to_img(5) & ":" & to_img(next_v(5)) & ":" & to_img(prev_v(5))
            & ":" & to_img(to_pos('A')) & ":" & to_val(66)
            & ":" & to_img(right_v(2)) & ":" & to_img(left_v(2));
    end function;
end package body;

entity gentype12 is
end entity;

architecture test of gentype12 is
    package inst is new work.gentype12_pkg
        generic map (
            to_img  => integer'image,
            next_v  => integer'succ,
            prev_v  => integer'pred,
            to_pos  => character'pos,
            to_val  => character'val,
            right_v => integer'rightof,
            left_v  => integer'leftof
        );
begin
    chk : process is
    begin
        assert inst.eval_all = "5:6:4:65:B:3:1" report inst.eval_all;
        wait;
    end process;
end architecture;
