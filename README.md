<!-- PROJECT LOGO -->
<br />
<div align="center">
<h3 align="center">Redis</h3>

  <p align="center">
    A clone of Redis server!
    <br />
    <a href="https://github.com/sleepyviper1213/redis">View Demo</a>
    &middot;
    <a href="https://github.com/sleepyviper1213/redis/issues/new?labels=bug&template=bug-report---.md">Report Bug</a>
    &middot;
    <a href="https://github.com/sleepyviper1213/redis/issues/new?labels=enhancement&template=feature-request---.md">Request Feature</a>
  </p>
</div>



<!-- TABLE OF CONTENTS -->
<details>
  <summary>Table of Contents</summary>
  <ol>
    <li>
      <a href="#about-the-project">About The Project</a>
      <ul>
        <li><a href="#built-with">Built With</a></li>
      </ul>
    </li>
    <li>
      <a href="#getting-started">Getting Started</a>
      <ul>
        <li><a href="#prerequisites">Prerequisites</a></li>
        <li><a href="#installation">Installation</a></li>
      </ul>
    </li>
    <li><a href="#usage">Usage</a></li>
    <li><a href="#roadmap">Roadmap</a></li>
    <li><a href="#contributing">Contributing</a></li>
    <li><a href="#license">License</a></li>
    <li><a href="#contact">Contact</a></li>
    <li><a href="#acknowledgments">Acknowledgments</a></li>
  </ol>
</details>



<!-- ABOUT THE PROJECT -->
# About The Project

[![Product Name Screen Shot][product-screenshot]](https://example.com)

This project is a simplified reimplementation of Redis, designed as both a personal learning journey and an open-source initiative to explore the internals of high-performance in-memory key-value stores using modern C++ (C++23).

Redis was chosen as the foundation because it presents a rich set of real-world challenges across:

- Event-driven asynchronous networking 
- Efficient data structure design 
- Low-latency I/O handling 
- Lightweight protocol parsing 
- Careful memory and resource management

The goals of this project are:

- To build a maintainable and readable Redis-like server using modern C++ features such as std::expected, ranges, and advanced concurrency tools
- To create a platform for learning and experimenting with scalable server architecture
- To provide a starting point for contributors interested in systems programming, performance optimization, and networked application design

This project welcomes contributions, feedback, and discussion. If you're interested in collaborating, improving performance, adding features, or refactoring components, feel free to open issues, submit pull requests, or reach out!

## Built With

This section should list any major frameworks/libraries used to bootstrap your project. Leave any add-ons/plugins for the acknowledgements section. Here are a few examples.



[![Boost.Beast](https://img.shields.io/badge/Boost.Beast-1.88-blue)](https://www.boost.org/library/latest/beast/)
[![spdlog](https://img.shields.io/badge/spdlog-1.15.3-blue)](https://github.com/gabime/spdlog/)
[![cereal](https://img.shields.io/badge/cereal-1.3.2-blue)](https://github.com/USCiLab/cereal)

<!-- GETTING STARTED -->
# Getting Started
## Prerequisites

### Supported Compilers and Environments

This project requires **C++23** because it uses `std::expected` as the primary error handling mechanism.

Additionally:
- The **GNU statement expression extension** (https://gcc.gnu.org/onlinedocs/gcc/Statement-Exprs.html) is used, so **MSVC is not currently supported**.
- The project relies on the system call from Unix-like APIs, i.e `fork()`, so **Windows is not supported**.

#### Minimum Tested Compiler Versions

| Compiler                                     | Minimum Version |
|----------------------------------------------|-----------------|
| GCC                                          | 13.0            |
| Clang                                        | 14.0            |
| Apple Clang (Xcode/Xcode Command Line Tools) | 14.0            |
| MSVC (Windows)                               | ❌ Not supported |

### CMake
If you want to install this project with **vcpkg** and **CMake Presets**, CMake 3.21+ is required. Otherwise, there is no requirement.
## Package Manager
A package manager is required to install dependencies. You can use either **Homebrew**(macOS), **apt**(Ubuntu) or 
**vcpkg**(cross-platform).

### Using vcpkg

1. Follow the official installation guide:  
   👉 https://learn.microsoft.com/en-us/vcpkg/get-started/get-started?pivots=shell-powershell

2. Clone the repository:
    ```sh
    git clone https://github.com/microsoft/vcpkg.git
    ````

3. Run the bootstrap script:
   ```sh
   cd vcpkg
   ./bootstrap-vcpkg.sh
   ```

4. Set the `VCPKG_ROOT` environment variable.
   Add the following to your shell config (e.g., `~/.zshrc`, `~/.bashrc`):

   ```sh
   echo 'export VCPKG_ROOT=/path/to/vcpkg' >> ~/.zshrc
   echo 'export PATH=$VCPKG_ROOT:$PATH' >> ~/.zshrc
   ```

---

### Using Homebrew (macOS)

1. Install **Homebrew**:

   ```sh
   /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
   ```

2. Install dependencies:

   ```sh
   brew install cmake spdlog boost cereal
   ```

---



### Using APT (Debian/Ubuntu)

> 📌 Note: Some packages may be outdated in the default APT repositories. For full C++23 support, consider using a newer compiler via a PPA or manually built toolchain.

1. Update package lists:
    ```sh
    sudo apt update
    ````

2. Install dependencies:

   ```sh
   sudo apt install g++ cmake libboost-all-dev libspdlog-dev cereal
   ```

3. (Optional) Install **Cereal** via source or **vcpkg** if not available:

   ```sh
   git clone https://github.com/USCiLab/cereal.git
   sudo cp -r cereal/include/cereal /usr/local/include/
   ```

    > ⚠️ If your system's default compiler doesn't support `std::expected`, install a newer GCC (e.g. 13) via PPA:

    ```sh
    sudo add-apt-repository ppa:ubuntu-toolchain-r/test
    sudo apt update
    sudo apt install g++-13
    ```

    Then, configure CMake to use it:

    ```sh
    export CXX=g++-13
    export CC=gcc-13
    ```

## Installation
1. Clone this project
    ```shell
    git clone https://github.com/sleepyviper1213/redis.git && cd redis
    ```
2. Configure the build using **CMake**
    If you want to configure with **vcpkg** and **CMake Presets**
   ```sh
   cmake --preset=vcpkg
   ```
   Otherwise, configure manually:
   ```sh
   cmake -S . -B build
   ```
3. Build the project
    ```sh
    cmake --build build
    ```
4. Run the application
    ```shell
    build/src/redis
    ```



<!-- USAGE EXAMPLES -->
# Usage

Use this space to show useful examples of how a project can be used. Additional screenshots, code examples and demos work well in this space. You may also link to more resources.

_For more examples, please refer to the [Documentation](https://example.com)_

<p align="right">(<a href="#readme-top">back to top</a>)</p>



<!-- ROADMAP -->
# Roadmap

- [ ] Add README.md


See the [open issues](https://github.com/sleepyviper1213/redis/issues) for a full list of proposed features (and known issues).





<!-- LICENSE -->
# License
Distributed under the MIT License. See `LICENSE` for more information.




<!-- CONTACT -->
# Contact

Khac Truong Nguyen - [@khactruongnguyen](https://www.linkedin.com/in/khactruongnguyen/) - truong_dev@icloud.com