-- LCS-2016-014a: access types designating composites of protected types,
-- and access type definitions and allocators with a generic map aspect
-- for an uninstantiated protected type (IEEE 1076-2019 5.4.1, 9.3.7,
-- 14.4.2.3, 14.6).

entity protected17 is
end entity;

architecture test of protected17 is
    type counter_t is protected
        procedure inc;
        impure function get return natural;
    end protected;

    type counter_t is protected body
        variable n : natural := 0;
        procedure inc is
        begin
            n := n + 1;
        end procedure;
        impure function get return natural is
        begin
            return n;
        end function;
    end protected body;

    type counter_vec is array (natural range <>) of counter_t;
    type counter_vec_ptr is access counter_vec;

    type counter_rec is record
        a, b : counter_t;
    end record;
    type counter_rec_ptr is access counter_rec;

    -- Uninstantiated protected type
    type cell_t is protected
        generic ( type t; init : t; id : integer := 0 );
        procedure set (x : t);
        impure function get return t;
        impure function get_id return integer;
        procedure bump;
        impure function bumps return natural;
    end protected;

    type cell_t is protected body
        variable v : t := init;
        variable nbumps : natural := 0;
        procedure set (x : t) is
        begin
            v := x;
        end procedure;
        impure function get return t is
        begin
            return v;
        end function;
        impure function get_id return integer is
        begin
            return id;
        end function;
        procedure bump is
        begin
            nbumps := nbumps + 1;
        end procedure;
        impure function bumps return natural is
        begin
            return nbumps;
        end function;
    end protected body;

    -- Access to an uninstantiated protected type without a generic map
    -- aspect: the generic map is given by each allocator
    type cell_ptr is access cell_t;

    -- Access type definition with a generic map aspect
    type int_cell_ptr is access cell_t generic map (t => integer, init => 7);
begin

    p1: process is
        variable vp : counter_vec_ptr;
        variable rp : counter_rec_ptr;
        variable cp1, cp2 : cell_ptr;
        variable ip1, ip2 : int_cell_ptr;
    begin
        -- Allocator for an unconstrained array of protected type
        vp := new counter_vec(1 to 3);
        assert vp'length = 3;
        assert vp.all'length = 3;
        vp(1).inc;
        vp.all(3).inc;
        vp(3).inc;
        assert vp(1).get = 1;
        assert vp(2).get = 0;
        assert vp(3).get = 2;
        deallocate(vp);
        assert vp = null;

        rp := new counter_rec;
        rp.a.inc;
        rp.all.b.inc;
        rp.b.inc;
        assert rp.a.get = 1;
        assert rp.b.get = 2;
        deallocate(rp);

        -- Allocator with a generic map aspect.  Through an access to the
        -- uninstantiated type only methods whose profiles do not depend on
        -- the generics are called.
        cp1 := new cell_t generic map (t => integer, init => 5, id => 11);
        assert cp1.get_id = 11;
        cp1.bump;
        cp1.bump;
        assert cp1.bumps = 2;

        -- Allocators for the same uninstantiated type create distinct
        -- objects, and may use different actuals
        cp2 := new cell_t generic map (t => bit_vector(1 to 3), init => "101",
                                       id => 22);
        assert cp2.get_id = 22;
        assert cp2.bumps = 0;
        assert cp1.bumps = 2;

        -- Access type whose definition has a generic map aspect
        ip1 := new int_cell_ptr'designated_subtype;
        assert ip1.get = 7;
        ip1.set(-3);
        ip2 := new int_cell_ptr'designated_subtype;
        assert ip2.get = 7;
        assert ip1.get = -3;

        deallocate(cp1);
        deallocate(cp2);
        deallocate(ip1);
        deallocate(ip2);
        wait;
    end process;

end architecture;
