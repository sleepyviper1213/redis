# One directory per library target; dependencies point strictly downward:
#
#     server ──► core
#
# Include paths are rooted at src/, so a header is always included by its
# module-qualified path ("core/resp/parser.hpp", "server/net/session.hpp").
add_subdirectory(core)
add_subdirectory(server)
