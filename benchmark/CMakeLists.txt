# CMakeLists.txt snippet
#find_package(benchmark REQUIRED)
find_package(Catch2 3 REQUIRED)
find_package(absl REQUIRED)
find_package(xxhash REQUIRED)
add_executable(hash_bench benchmark.cpp static_hash_map.cpp)
target_link_libraries(hash_bench Catch2::Catch2WithMain absl::flat_hash_map
                      xxhash )
#target_compile_options(hash_bench PUBLIC -fsanitize=address)
#target_link_options(hash_bench PUBLIC -fsanitize=address)
