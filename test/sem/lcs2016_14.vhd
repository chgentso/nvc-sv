-- LCS-2016-014: composites of protected types and of file types.
-- IEEE 1076-2019 4.2.1, 4.2.2.2, 5.3.1, 5.5.1, 6.4.2.2-6.4.2.5, 6.5.2,
-- 6.7, 8.3, 9.2.3, 10.6.2.1

entity lcs2016_14 is
end entity;

architecture test of lcs2016_14 is
    type pt is protected
        procedure proc;
        impure function get return integer;
    end protected;

    type pt is protected body
        variable v : integer;
        procedure proc is
        begin
            v := v + 1;
        end procedure;
        impure function get return integer is
        begin
            return v;
        end function;
    end protected body;

    type pt_array is array (natural range <>) of pt;   -- OK
    type pt_rec is record                               -- OK
        a, b : pt;
    end record;
    type pt_nest is array (1 to 2) of pt_rec;           -- OK

    type mixed_t is record                              -- Error
        p : pt;
        i : integer;
    end record;

    type ft is file of integer;
    type ft_array is array (1 to 2) of ft;              -- OK
    type ft_rec is record                               -- OK
        f, g : ft;
    end record;

    type mixedf_t is record                             -- Error
        f : ft;
        i : integer;
    end record;

    type mixedpf_t is record                            -- Error
        f : ft;
        p : pt;
    end record;

    shared variable sv1 : pt_array(1 to 3);             -- OK
    shared variable sv2 : pt_rec;                       -- OK
    signal s1 : pt_array(1 to 2);                       -- Error

    type pt_array_file is file of pt_array;             -- Error

    function f1 return pt_array;                        -- Error
    function f2 return ft_rec;                          -- Error

    procedure p1 (x : in pt_array) is                   -- Error
    begin
    end procedure;

    procedure p2 (x : inout pt_array) is                -- OK
    begin
        x(x'left).proc;                                 -- OK
    end procedure;

    procedure p3 (file x : ft_array) is                 -- OK
    begin
    end procedure;

    procedure p4 (x : ft_array) is                      -- Error
    begin
    end procedure;

    attribute att : pt_array;                           -- Error

    file fa1 : ft_array;                                -- OK
    file fa2 : ft_array open read_mode is "foo";        -- Error
begin

    process is
        variable v1 : pt_array(1 to 2);                 -- OK
        variable v2 : pt_array(1 to 2);
        variable v3 : pt_rec;                           -- OK
        variable v4 : pt_array(1 to 2) := v2;           -- Error
        variable v5 : pt_nest;                          -- OK
    begin
        v1(1).proc;                                     -- OK
        v3.a.proc;                                      -- OK
        v5(2).b.proc;                                   -- OK
        assert v1(2).get = 0;                           -- OK
        v1 := v2;                                       -- Error
        v3.a := v3.b;                                   -- Error
        assert v1 = v2;                                 -- Error
        v1.proc;                                        -- Error
        p2(v1);                                         -- OK
        p2(sv1);                                        -- OK
        sv1(2).proc;                                    -- OK
        sv2.b.proc;                                     -- OK
        p3(fa1);                                        -- OK
        wait;
    end process;

end architecture;
