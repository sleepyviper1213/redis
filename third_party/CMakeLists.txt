# Add t1ha library
add_library(t1ha 
    t1ha/src/t1ha0.c
    t1ha/src/t1ha1.c
    t1ha/src/t1ha2.c
    t1ha/src/t1ha_selfcheck.c
)

# Explicitly tell CMake it’s a C library
set_target_properties(t1ha PROPERTIES LINKER_LANGUAGE C)



