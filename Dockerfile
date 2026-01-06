FROM ubuntu:latest

# Non-interactive APT
ENV DEBIAN_FRONTEND=noninteractive

# Install build tools and runtime libraries (including OpenMP runtime)
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       build-essential \
       cmake \
       gcc \
       g++ \
       make \
       gdb \
       python3 \
       python3-pip \
       libgomp1 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

# Copy only the project skeleton contents
COPY ./project_skeleton /workspace

# Build the project during image build so the resulting image contains the binaries
RUN make -C .

# open an interactive shell
CMD ["/bin/bash"]
