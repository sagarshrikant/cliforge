# =============================================================================
#  cliforge development container
# -----------------------------------------------------------------------------
#  Ubuntu 22.04 — chosen deliberately to MIRROR the CI runners
#  (.github/workflows use runs-on: ubuntu-22.04 with clang-14).  Building and
#  testing inside this image reproduces CI results locally with the fewest
#  surprises.
#
#  Think of this image as a "CI runner you can SSH into": the same compilers
#  and tools the pipeline uses, plus the extras a human needs to develop
#  comfortably (gdb, docs toolchain, the AI test advisor, packaging tools).
#
#  Build:   docker build -t cliforge-dev -f .devcontainer/Dockerfile .
#  Run:     docker run --rm -it -v "$PWD":/workspaces/cliforge cliforge-dev
#  (Or just "Reopen in Container" from VS Code — see devcontainer.json.)
# =============================================================================
FROM ubuntu:22.04

# Avoid interactive tzdata/debconf prompts during apt installs.
ENV DEBIAN_FRONTEND=noninteractive \
    TZ=Etc/UTC \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8

# -----------------------------------------------------------------------------
# Core build toolchain + dev tooling, in a single layer.
#
#  Group                What it gives you
#  -------------------  ----------------------------------------------------
#  build-essential      make + the GNU toolchain plumbing
#  gcc / g++            primary C/C++ compilers (matches CI "gcc" matrix leg)
#  clang-14 / clang++   secondary compiler (matches CI "clang-14" matrix leg)
#  cmake / ninja-build  the project's build system + fast generator
#  gdb                  interactive debugger used by the VS Code F5 flow
#  lcov                 coverage capture + genhtml reports
#  python3 / pip        runs tools/utest_agent (AI test advisor)
#  doxygen / graphviz   API reference extraction (+ diagrams)
#  sphinx + breathe     reference-guide site that consumes Doxygen XML
#  dpkg-dev / fakeroot  build + test .deb packages locally
#  rpm                  build + test .rpm packages locally
#  zstd                 fast (de)compression used by some packaging steps
#  git / curl / ca-cert base hygiene
# -----------------------------------------------------------------------------
RUN apt-get update -qq && \
    apt-get install -y --no-install-recommends \
        build-essential \
        gcc g++ \
        clang-14 clang-tools-14 \
        cmake \
        ninja-build \
        gdb \
        lcov \
        valgrind \
        python3 python3-pip python3-venv \
        doxygen graphviz \
        dpkg-dev fakeroot \
        rpm \
        zstd \
        git curl ca-certificates \
        less nano sudo locales \
    && rm -rf /var/lib/apt/lists/*

# Make `clang` / `clang++` resolve to the versioned binaries CI uses, so
# CC=clang works the same inside the container as in the pipeline.
RUN update-alternatives --install /usr/bin/clang   clang   /usr/bin/clang-14   100 && \
    update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-14 100

# -----------------------------------------------------------------------------
# Documentation toolchain (Python side) and the AI test advisor dependencies.
#
# Sphinx + Breathe + the Furo theme render the reference guide from Doxygen
# XML.  The utest_agent requirements are layered in too so `python3
# tools/utest_agent/agent.py` works out of the box (Claude backend just needs
# ANTHROPIC_API_KEY at runtime).
# -----------------------------------------------------------------------------
RUN pip3 install --no-cache-dir \
        sphinx \
        breathe \
        furo \
        sphinx-copybutton \
        myst-parser

# utest_agent Python deps — copied separately so this layer caches on the
# requirements file alone.  Tolerate absence so the image still builds if the
# file is ever moved.
COPY tools/utest_agent/requirements.txt /tmp/utest-requirements.txt
RUN pip3 install --no-cache-dir -r /tmp/utest-requirements.txt || true

# -----------------------------------------------------------------------------
# Ollama — offline backend for the AI test advisor (no API key needed).
#
# We install the Ollama *binary* only.  The ~5 GB model is NOT baked into the
# image (that would bloat it enormously and rarely matches what a given
# contributor wants).  Pull it on first use, inside the running container:
#
#     bash tools/utest_agent/setup_ollama.sh      # installs service + pulls model
#     # or simply:
#     ollama serve & ollama pull qwen2.5-coder:7b
#
# The Claude backend needs no model download — just export ANTHROPIC_API_KEY.
# -----------------------------------------------------------------------------
RUN curl -fsSL https://ollama.com/install.sh | sh || \
    echo "WARNING: Ollama install skipped (offline build?). Run setup_ollama.sh later."

# -----------------------------------------------------------------------------
# Non-root development user.
#
# Matching UID/GID 1000 keeps file ownership sane on bind-mounted source on
# most Linux hosts.  VS Code attaches as this user (see devcontainer.json).
# -----------------------------------------------------------------------------
ARG USERNAME=dev
ARG USER_UID=1000
ARG USER_GID=1000
RUN groupadd --gid ${USER_GID} ${USERNAME} 2>/dev/null || true && \
    useradd  --uid ${USER_UID} --gid ${USER_GID} -m -s /bin/bash ${USERNAME} 2>/dev/null || true && \
    echo "${USERNAME} ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/${USERNAME} && \
    chmod 0440 /etc/sudoers.d/${USERNAME}

USER ${USERNAME}
WORKDIR /workspaces/cliforge

# Friendly default: drop into a shell that already has the toolchain on PATH.
CMD ["/bin/bash"]
