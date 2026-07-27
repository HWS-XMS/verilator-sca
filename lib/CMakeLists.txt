add_library(sca STATIC
    src/hdf5_writer.cpp
    src/config.cpp
    src/leakage_model.cpp
    src/trace_buffer.cpp
    src/signal_reader.cpp
    src/sca.cpp
)

target_include_directories(sca PUBLIC include)

# Verilator headers (for direct memory access)
find_package(verilator REQUIRED)
target_include_directories(sca PRIVATE
    ${VERILATOR_ROOT}/include
    ${VERILATOR_ROOT}/include/vltstd
)

# yaml-cpp
find_package(yaml-cpp REQUIRED)
target_link_libraries(sca PUBLIC yaml-cpp::yaml-cpp)

# HDF5
find_package(HDF5 REQUIRED COMPONENTS C)
target_link_libraries(sca PUBLIC HDF5::HDF5)
