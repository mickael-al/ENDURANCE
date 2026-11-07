#!/bin/bash

usage() {
    echo "Usage: $0 [Debug|Release|RelWithDebInfo] [--no-warnings|--warnings]"
    echo "Warnings are enabled by default."
}

buildType=Debug
buildTypeSet=false
enableWarnings=ON
for arg in "$@"; do
    case "$arg" in
        --no-warnings) enableWarnings=OFF ;;
        --warnings) enableWarnings=ON ;;
        -h|--help) usage; exit 0 ;;
        -*) echo "Error: unknown option '$arg'." >&2; usage; exit 1 ;;
        *)
            if "$buildTypeSet"; then
                echo "Error: only one build type may be supplied." >&2
                usage
                exit 1
            fi
            buildType="$arg"
            buildTypeSet=true
            ;;
    esac
done

case "$buildType" in
    Debug|Release|RelWithDebInfo) ;;
    *) echo "Error: unsupported build type '$buildType'." >&2; usage; exit 1 ;;
esac

# --- Initial configuration ---
projectDir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd) || exit 1
buildDir="$projectDir/build/$buildType"

# --- Check if cmake is installed ---
if ! command -v cmake &> /dev/null; then
    echo "❌ Error: cmake is not installed."
    exit 1
fi

# --- Detect available C++ compiler ---
if command -v g++ &> /dev/null; then
    cxx_compiler=$(command -v g++)
    compiler_name="GCC"
elif command -v clang++ &> /dev/null; then
    cxx_compiler=$(command -v clang++)
    compiler_name="Clang"
else
    echo "❌ Error: No compatible compiler found (GCC or Clang required)."
    exit 1
fi

echo "✅ Detected compiler: $compiler_name ($cxx_compiler)"
echo "Compiler warnings: $enableWarnings"
cmakeWarningArgs=(-Wdev -Wdeprecated)
if [[ "$enableWarnings" == OFF ]]; then
    cmakeWarningArgs=(-Wno-dev -Wno-deprecated)
fi

# --- Create build output directory ---
mkdir -p "$buildDir" || exit 1
cd "$buildDir" || exit 1

# --- Run CMake generation ---
echo "🛠️  Running CMake in $buildDir"
cmake "$projectDir" \
    "${cmakeWarningArgs[@]}" \
    "-DCMAKE_BUILD_TYPE=$buildType" \
    "-DCMAKE_CXX_COMPILER=$cxx_compiler" \
    "-DANTAGONIZ_ENABLE_WARNINGS=$enableWarnings" || exit $?

# --- Build the project ---
echo "🔧 Building..."
cmake --build . --config "$buildType" || exit $?

echo "✅ Build completed. Executable: $projectDir/Bin/ENDURANCE"
