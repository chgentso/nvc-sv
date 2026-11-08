-- LCS-2016-034: protected types with a generic clause, and LCS-2016-014a
-- generic map aspects on access type definitions and allocators
-- (IEEE 1076-2019 5.4.1, 5.6.2, 5.6.4, 6.4.2.4, 9.3.7)

entity lcs2016_34 is
end entity;

architecture test of lcs2016_34 is
    type simple_t is protected          -- Simple protected type
        procedure proc;
    end protected;

    type simple_t is protected body
        procedure proc is
        begin
        end procedure;
    end protected body;

    type gen_t is protected             -- Uninstantiated protected type
        generic ( type t; n : integer );
        procedure put (x : t);
    end protected;

    type gen_t is protected body
        procedure put (x : t) is
        begin
        end procedure;
    end protected body;

    type dflt_t is protected            -- All generics have defaults
        generic ( n : integer := 5 );
        impure function get return integer;
    end protected;

    type dflt_t is protected body
        impure function get return integer is
        begin
            return n;
        end function;
    end protected body;

    type mapped_t is protected          -- Generic-mapped protected type
        generic ( n : integer );
        generic map ( n => 1 );
        procedure proc;
    end protected;

    type mapped_t is protected body
        procedure proc is
        begin
        end procedure;
    end protected body;

    type inst1_t is new gen_t generic map (t => bit, n => 2);  -- OK
    type inst2_t is new gen_t generic map (t => bit);  -- Error
    type inst3_t is new gen_t generic map (t => bit, n => 1, n => 2);  -- Error
    type inst4_t is new simple_t;       -- Error
    type inst5_t is new mapped_t generic map (n => 2);  -- Error
    type inst6_t is new dflt_t;         -- OK

    shared variable sv1 : inst1_t;      -- OK
    shared variable sv2 : gen_t generic map (t => integer, n => 3);  -- OK
    shared variable sv3 : gen_t;        -- Error
    shared variable sv4 : simple_t generic map (n => 1);  -- Error
    shared variable sv5 : dflt_t;       -- OK
    shared variable sv6 : mapped_t;     -- OK

    type ptr1_t is access gen_t generic map (t => real, n => 1);  -- OK
    type ptr2_t is access gen_t;        -- OK
    type ptr3_t is access simple_t generic map (n => 1);  -- Error
    type ptr4_t is access simple_t;     -- OK
begin

    process is
        variable v1 : gen_t generic map (t => bit, n => 1);  -- OK
        variable v2 : integer generic map (n => 1);  -- Error
        variable p2 : ptr2_t;
        variable p4 : ptr4_t;
    begin
        v1.put('1');                    -- OK
        sv2.put(5);                     -- OK
        assert sv5.get = 5;             -- OK
        p2 := new gen_t generic map (t => integer, n => 1);  -- OK
        p2 := new gen_t;                -- Error
        p4 := new simple_t generic map (n => 1);  -- Error
        wait;
    end process;

end architecture;
