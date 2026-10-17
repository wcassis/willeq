# shellcheck shell=bash
# B1087 (#407): a persistent ccache for the ARM cross-builds (scripts/build-arm-noble.sh,
# scripts/build-arm64-noble.sh). Sourced, not run.
#
# The CI runner has one checkout per concurrency slot, so the in-tree incremental cache
# (build-arm*-noble/cache) is cold the first time an ARM job lands in a slot. The compile
# runs in a container that always sees the source at /src and the tree at /build, so the
# compile lines are identical in every slot and one host ccache directory serves them all.
#
# ARM_CCACHE_DIR names that host directory (the CI job sets it; unset = no ccache, which is
# what a hand run on a dev box gets). It must already exist and be writable: the job never
# creates it, so a host that was not provisioned builds exactly as before. Each image gets
# its own subdirectory (the armhf and aarch64 compilers never share an object) capped at
# ARM_CCACHE_MAXSIZE (default 10G, so the two fit the host's 20G budget).
#
# arm_ccache_docker_args <image-name>  -> fills the array ARM_CCACHE_ARGS with the
#                                         `docker run` mount + env arguments (or nothing)
arm_ccache_docker_args() {
    local image="$1" dir
    ARM_CCACHE_ARGS=()
    if [ -z "${ARM_CCACHE_DIR:-}" ]; then
        echo "ccache:        off (ARM_CCACHE_DIR unset)"
        return 0
    fi
    if [ ! -d "$ARM_CCACHE_DIR" ] || [ ! -w "$ARM_CCACHE_DIR" ]; then
        echo "ccache:        off ($ARM_CCACHE_DIR is not a writable directory on $(hostname))"
        return 0
    fi
    dir="$ARM_CCACHE_DIR/$image"
    mkdir -p "$dir"
    ARM_CCACHE_ARGS=(
        -v "$dir:/ccache"
        -e CCACHE_DIR=/ccache
        -e "CCACHE_MAXSIZE=${ARM_CCACHE_MAXSIZE:-10G}"
        # The container's checkout root; with NOHASHDIR the -g build's cwd stays out of the hash.
        -e CCACHE_BASEDIR=/src
        -e CCACHE_NOHASHDIR=1
        # This build's own hits/misses (the cache totals mix concurrent jobs).
        -e CCACHE_STATSLOG=/build/ccache-stats.log
    )
    echo "ccache:        $dir (max ${ARM_CCACHE_MAXSIZE:-10G})"
}
