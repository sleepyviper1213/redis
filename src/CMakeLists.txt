# One directory per library target; dependencies point strictly downward:
#
#     server ──► common ──► core
#
# Include paths are rooted at src/, so a header is always included by its
# module-qualified path ("common/resp/parser.hpp", "server/net/session.hpp").
add_subdirectory(core)
add_subdirectory(common)
add_subdirectory(server)
add_subdirectory(app)
