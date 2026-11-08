-- LCS-2016-034: protected types with a generic clause.  Uninstantiated
-- protected types, protected type instantiation declarations, variable
-- declarations with a generic map aspect, and generic-mapped protected
-- types (IEEE 1076-2019 5.6.2, 5.6.4, 6.4.2.4, 6.5.6.2, 6.5.7.2).

package fifo_pkg is
    -- Uninstantiated protected type with type, constant and subprogram
    -- generics
    type fifo_t is protected
        generic (
            type element_t;
            depth : positive := 4;
            function to_string (x : element_t) return string is <> );
        procedure push (x : element_t);
        impure function pop return element_t;
        impure function count return natural;
        impure function capacity return natural;
        impure function peek_image return string;
    end protected;

    -- Protected type instantiation declaration in a package: the body
    -- of the generic-mapped protected type is implicitly at the end of
    -- the package body
    type int_fifo_t is new fifo_t generic map (element_t => integer);
end package;

package body fifo_pkg is
    type fifo_t is protected body
        type store_t is array (0 to depth - 1) of element_t;
        variable store : store_t;
        variable head, used : natural := 0;

        procedure push (x : element_t) is
        begin
            assert used < depth report "fifo full" severity failure;
            store((head + used) mod depth) := x;
            used := used + 1;
        end procedure;

        impure function pop return element_t is
            constant result : element_t := store(head);
        begin
            assert used > 0 report "fifo empty" severity failure;
            head := (head + 1) mod depth;
            used := used - 1;
            return result;
        end function;

        impure function count return natural is
        begin
            return used;
        end function;

        impure function capacity return natural is
        begin
            return depth;
        end function;

        impure function peek_image return string is
        begin
            return to_string(store(head));
        end function;
    end protected body;
end package body;

-------------------------------------------------------------------------------

entity protected16 is
end entity;

use work.fifo_pkg.all;

architecture test of protected16 is
    function bv_image (x : bit_vector) return string is
    begin
        return "bv:" & to_string(x);
    end function;

    -- Instantiation declarations in an architecture
    type real_fifo_t is new fifo_t
        generic map (element_t => real, depth => 2);
    type bv_fifo_t is new fifo_t
        generic map (element_t => bit_vector(1 to 4), depth => 3,
                     to_string => bv_image);

    -- Shared variables of instantiated protected types
    shared variable sf1 : int_fifo_t;
    shared variable sf2 : real_fifo_t;

    -- Shared variable declaration with a generic map aspect
    shared variable sf3 : fifo_t generic map (element_t => character,
                                              depth => 8);

    -- Generic-mapped protected type: generic clause and generic map
    -- aspect in the same header
    type acc_t is protected
        generic ( type t is range <>; init : t );
        generic map ( t => integer, init => 100 );
        procedure add (x : t);
        impure function get return t;
    end protected;

    type acc_t is protected body
        variable total : t := init;

        procedure add (x : t) is
        begin
            total := total + x;
        end procedure;

        impure function get return t is
        begin
            return total;
        end function;
    end protected body;

    shared variable acc : acc_t;
begin

    p1: process is
        variable lf1 : int_fifo_t;
        variable lf2 : bv_fifo_t;
        -- Variable declaration with a generic map aspect; DEPTH takes its
        -- default
        variable lf3 : fifo_t generic map (element_t => boolean);
        variable lf4 : fifo_t generic map (element_t => boolean);
    begin
        -- Default generic values
        assert lf1.capacity = 4;
        assert sf1.capacity = 4;
        assert lf3.capacity = 4;
        assert sf2.capacity = 2;
        assert lf2.capacity = 3;
        assert sf3.capacity = 8;

        lf1.push(10);
        lf1.push(20);
        lf1.push(30);
        assert lf1.count = 3;
        assert lf1.peek_image = "10";   -- Default TO_STRING matched by <>
        assert lf1.pop = 10;
        assert lf1.pop = 20;
        lf1.push(40);
        assert lf1.pop = 30;
        assert lf1.pop = 40;
        assert lf1.count = 0;

        sf2.push(1.5);
        sf2.push(2.5);
        assert sf2.count = 2;
        assert sf2.pop = 1.5;
        assert sf2.pop = 2.5;

        lf2.push("1010");
        lf2.push("0110");
        assert lf2.peek_image = "bv:1010";  -- Explicit TO_STRING actual
        assert lf2.pop = "1010";
        assert lf2.pop = "0110";

        for c in character'('a') to 'h' loop
            sf3.push(c);
        end loop;
        assert sf3.count = 8;
        assert sf3.pop = 'a';
        assert sf3.pop = 'b';
        assert sf3.count = 6;

        -- Two variables declared with identical generic map aspects are
        -- distinct objects
        lf3.push(true);
        assert lf3.count = 1;
        assert lf4.count = 0;
        assert lf3.peek_image = "true";

        -- Instances in different scopes are independent
        sf1.push(5);
        assert sf1.count = 1;
        assert lf1.count = 0;
        assert sf1.pop = 5;

        assert acc.get = 100;
        acc.add(23);
        assert acc.get = 123;

        wait;
    end process;

end architecture;
