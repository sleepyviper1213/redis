set(CMAKE_CXX_COMPILER /opt/homebrew/opt/llvm/bin/clang++)
set(CMAKE_LINKER "opt/homebrew/opt/lld/bin/lld")

set(CMAKE_EXE_LINKER_FLAGS_INIT
	"-L/opt/homebrew/opt/llvm/lib/c++ -L/opt/homebrew/opt/llvm/lib/unwind -lunwind")
set(CMAKE_SHARED_LINKER_FLAGS_INIT
	"-L/opt/homebrew/opt/llvm/lib/c++ -L/opt/homebrew/opt/llvm/lib/unwind -lunwind")