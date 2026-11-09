FROM ubuntu:24.04

RUN apt-get update

# Required to compile N00B
RUN apt-get install -y clang-15
RUN apt-get install -y libclang-15-dev
RUN apt-get install -y lld-15
RUN apt-get install -y cmake
RUN apt-get install -y make
# First one that includes the `misc-include-cleaner` check
RUN apt-get install -y clang-tidy-17

# Handy when testing N00B with the N00B chain loader
RUN apt-get install -y patchelf

# Other development utilities
RUN apt-get install -y clang-format
