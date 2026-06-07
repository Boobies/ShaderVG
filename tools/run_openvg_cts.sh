#!/usr/bin/env bash
set -u

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)

cts_repo_url=${CTS_REPO_URL:-https://github.com/KhronosGroup/OpenVG-CTS.git}
cts_dir=${CTS_DIR:-/tmp/OpenVG-CTS}
cts_version=${CTS_VERSION:-1.1_CTS}
results_dir=${CTS_RESULTS_DIR:-$repo_root/cts-results/openvg-1.1}
make_cmd=${MAKE:-make}
cc_cmd=${CC:-gcc}
build_shadervg=1
clone_cts=0
update_cts=0
build_only=0
test_name=
list_file=
config_id=
verbose=1
extra_cts_args=()

usage()
{
    cat <<EOF
Usage: $0 [options] [-- extra CTS args]

Build and optionally run the Khronos OpenVG 1.1 CTS generation harness against
the uninstalled ShaderVG libraries in this checkout.

Options:
  --cts-dir DIR              CTS checkout to use (default: $cts_dir)
  --results-dir DIR          Answer/info/log output directory (default: $results_dir)
  --cts-version NAME         CTS version directory (default: $cts_version)
  --clone                    Clone the CTS checkout if --cts-dir does not exist
  --update                   Run 'git pull --ff-only' in an existing CTS checkout
  --no-build-shadervg        Do not run make in the ShaderVG checkout first
  --build-only               Build the CTS generator but do not run it
  --test ID                  Run one CTS test id, for example A10101
  --list-file FILE           Run test ids listed in FILE
  --config ID                Run one EGL config id
  --verbose N                CTS verbose level, 0 through 2 (default: $verbose)
  -h, --help                 Show this help

Environment:
  CTS_REPO_URL               CTS repository URL
  CTS_DIR                    Same as --cts-dir
  CTS_RESULTS_DIR            Same as --results-dir
  CTS_VERSION                Same as --cts-version
  CTS_CFLAGS_EXTRA           Extra CFLAGS for the CTS build
  CTS_LDFLAGS_EXTRA          Extra LDFLAGS for the CTS build
  CTS_LIBS_EXTRA             Extra libraries for the CTS build
  MAKE, CC                   Build tools

Examples:
  $0 --clone --build-only
  $0 --cts-dir /tmp/OpenVG-CTS --test A10101
  $0 --cts-dir /tmp/OpenVG-CTS --list-file smoke-tests.txt
EOF
}

die()
{
    echo "error: $*" >&2
    exit 1
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --cts-dir)
            [ "$#" -ge 2 ] || die "--cts-dir requires a directory"
            cts_dir=$2
            shift 2
            ;;
        --results-dir)
            [ "$#" -ge 2 ] || die "--results-dir requires a directory"
            results_dir=$2
            shift 2
            ;;
        --cts-version)
            [ "$#" -ge 2 ] || die "--cts-version requires a directory name"
            cts_version=$2
            shift 2
            ;;
        --clone)
            clone_cts=1
            shift
            ;;
        --update)
            update_cts=1
            shift
            ;;
        --no-build-shadervg)
            build_shadervg=0
            shift
            ;;
        --build-only)
            build_only=1
            shift
            ;;
        --test)
            [ "$#" -ge 2 ] || die "--test requires a CTS test id"
            test_name=$2
            shift 2
            ;;
        --list-file)
            [ "$#" -ge 2 ] || die "--list-file requires a file"
            list_file=$2
            shift 2
            ;;
        --config)
            [ "$#" -ge 2 ] || die "--config requires an EGL config id"
            config_id=$2
            shift 2
            ;;
        --verbose)
            [ "$#" -ge 2 ] || die "--verbose requires 0, 1, or 2"
            verbose=$2
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            extra_cts_args=("$@")
            break
            ;;
        *)
            die "unknown option '$1'"
            ;;
    esac
done

[ -z "$test_name" ] || [ -z "$list_file" ] || die "--test and --list-file are mutually exclusive"

if [ ! -d "$cts_dir" ]; then
    if [ "$clone_cts" -eq 1 ]; then
        git clone --depth 1 "$cts_repo_url" "$cts_dir" || exit $?
    else
        die "CTS checkout not found: $cts_dir (use --clone or --cts-dir)"
    fi
elif [ "$update_cts" -eq 1 ]; then
    [ -d "$cts_dir/.git" ] || die "--update requires a git checkout: $cts_dir"
    git -C "$cts_dir" pull --ff-only || exit $?
fi

cts_root=$cts_dir/$cts_version
cts_make_dir=$cts_root/generation/make/linux
cts_makefile=$cts_make_dir/makefile
cts_bin=$cts_make_dir/bin/generator.exe

[ -f "$cts_makefile" ] || die "CTS Linux makefile not found: $cts_makefile"

if [ "$build_shadervg" -eq 1 ]; then
    [ -f "$repo_root/Makefile" ] || die "run ./configure before using this script"
    "$make_cmd" -C "$repo_root" || exit $?
fi

lib_dir=$repo_root/src/.libs
[ -f "$lib_dir/libOpenVG.so" ] || die "missing $lib_dir/libOpenVG.so; build ShaderVG first"
[ -f "$lib_dir/libShaderVGEGL.so" ] || die "missing $lib_dir/libShaderVGEGL.so; build ShaderVG first"

answer_dir=$results_dir/answer
info_dir=$results_dir/info
log_dir=$results_dir/logs
build_log=$log_dir/build.log

mkdir -p "$cts_make_dir/bin" "$answer_dir" "$info_dir" "$log_dir" || exit $?

cts_cflags="-std=gnu89 -I$repo_root/include -I../../source -include sys/stat.h -DEGL_CONFORMANT_KHR=EGL_CONFORMANT -DANSWER_DEFAULT_DIR=\\\"$answer_dir\\\" -DINFO_DEFAULT_DIR=\\\"$info_dir\\\" -DTEST_OPTION_VGU=1 -Wno-incompatible-pointer-types"
if [ -n "${CTS_CFLAGS_EXTRA:-}" ]; then
    cts_cflags="$cts_cflags $CTS_CFLAGS_EXTRA"
fi

cts_ldflags="-L$lib_dir -Wl,-rpath,$lib_dir -L/usr/X11R6/lib"
if [ -n "${CTS_LDFLAGS_EXTRA:-}" ]; then
    cts_ldflags="$cts_ldflags $CTS_LDFLAGS_EXTRA"
fi

cts_libs="-lShaderVGEGL -lOpenVG -lEGL -lX11 -lGL -ldl -lm"
if [ -n "${CTS_LIBS_EXTRA:-}" ]; then
    cts_libs="$cts_libs $CTS_LIBS_EXTRA"
fi

echo "Building OpenVG CTS generator..."
if ! "$make_cmd" -C "$cts_make_dir" -f makefile \
    "CC=$cc_cmd" \
    "CFLAGS=$cts_cflags" \
    "LD_FLAGS=$cts_ldflags" \
    "LIBS=$cts_libs" \
    >"$build_log" 2>&1; then
    echo "CTS build failed: $build_log" >&2
    if grep -Eq 'VG_A_[14]|VG_COLOR_TRANSFORM' "$build_log"; then
        echo "The full OpenVG 1.1 CTS currently exposes missing core API/header coverage." >&2
        echo "See TODO.md for the tracked image-format and color-transform blockers." >&2
    fi
    exit 1
fi
echo "CTS build log: $build_log"

if [ "$build_only" -eq 1 ]; then
    echo "CTS generator: $cts_bin"
    exit 0
fi

run_args=(-v "$verbose")
if [ -n "$config_id" ]; then
    run_args+=(-c "$config_id")
fi
if [ -n "$test_name" ]; then
    run_args+=(-1 "$test_name")
fi
if [ -n "$list_file" ]; then
    run_args+=(-f "$list_file")
fi
if [ "${#extra_cts_args[@]}" -gt 0 ]; then
    run_args+=("${extra_cts_args[@]}")
fi

timestamp=$(date +%Y%m%d-%H%M%S)
run_log=$log_dir/run-$timestamp.log

echo "Running OpenVG CTS generator..."
(
    export LD_LIBRARY_PATH="$lib_dir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    cd "$cts_make_dir" || exit $?
    "$cts_bin" "${run_args[@]}"
) >"$run_log" 2>&1
status=$?

echo "CTS run log: $run_log"
echo "CTS answer output: $answer_dir"
echo "CTS info output: $info_dir"
exit "$status"
