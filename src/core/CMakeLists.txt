add_library(
    core SHARED
    error/simple_message.cpp
    resp/parser.cpp
    resp/value.cpp
    utils/get_class_logger.cpp
    utils/deadline_timer.cpp
    utils/create_temp_file.cpp
    utils/toupper.cpp)

add_library(redis::core ALIAS core)

target_compile_options(core PUBLIC -fsanitize=address)
target_link_options(core PUBLIC -fsanitize=address)
target_link_libraries(core PUBLIC spdlog::spdlog magic_enum::magic_enum
                                  FastFloat::fast_float)

target_include_directories(
    core PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:${CMAKE_INCLUDE_DIR}>)
