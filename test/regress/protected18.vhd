-- LCS-2016-014a: access type definition with a generic map aspect for
-- an uninstantiated protected type (IEEE 1076-2019 5.4.1, 9.3.7,
-- 14.4.2.3, 14.6).  Requires protected types with a generic clause
-- (LCS-2016-034).

entity protected18 is
end entity;

architecture test of protected18 is
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

    -- Access type definition with a generic map aspect
    type int_cell_ptr is access cell_t generic map (t => integer, init => 7);
begin

    p1: process is
        variable ip1, ip2 : int_cell_ptr;
    begin
        -- Access type whose definition has a generic map aspect
        ip1 := new int_cell_ptr'designated_subtype;
        assert ip1.get = 7;
        ip1.set(-3);
        ip2 := new int_cell_ptr'designated_subtype;
        assert ip2.get = 7;
        assert ip1.get = -3;
        assert ip1.get_id = 0;          -- Default generic value
        ip1.bump;
        assert ip1.bumps = 1;
        assert ip2.bumps = 0;

        deallocate(ip1);
        deallocate(ip2);
        wait;
    end process;

end architecture;
