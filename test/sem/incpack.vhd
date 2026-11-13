package incpack is
    type rec;                           -- OK, completed in the body
    type rec_ptr is access rec;
end package;

package body incpack is
    type rec is record
        x : integer;
    end record;

    type opaque;                        -- Error
    type opaque_ptr is access opaque;

    type done;                          -- OK
    type done_ptr is access done;
    type done is record
        next_p : done_ptr;
    end record;
end package body;
