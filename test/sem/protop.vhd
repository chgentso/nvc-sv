entity protop is
end entity;

architecture test of protop is
    type acc is protected
        impure function "*" (l, r : integer) return integer;  -- Error
        impure function "not" (k : integer) return integer;  -- Error
        impure function "-" return integer;  -- OK
        impure function "-" (k : integer) return integer;  -- OK
        impure function "-" (k, j : integer) return integer;  -- Error
    end protected;

    type acc is protected body
        impure function "*" (l, r : integer) return integer is  -- Error
        begin
            return 0;
        end function;
        impure function "not" (k : integer) return integer is  -- Error
        begin
            return 0;
        end function;
        impure function "-" return integer is
        begin
            return 0;
        end function;
        impure function "-" (k : integer) return integer is
        begin
            return 0;
        end function;
        impure function "-" (k, j : integer) return integer is  -- Error
        begin
            return 0;
        end function;
    end protected body;
begin
end architecture;
