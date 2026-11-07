#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <Bullet build directory containing src/BulletDynamics, src/BulletCollision, src/LinearMath>" >&2
    exit 2
fi
endurance_test_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
endurance_bullet_build="$(cd -- "$1" && pwd)"
endurance_test_binary="$(mktemp "${TMPDIR:-/tmp}/endurance-physics-worker.XXXXXX")"
trap 'rm -f -- "$endurance_test_binary"' EXIT
cd -- "$endurance_test_root"

"${CXX:-g++}" -std=c++17 -O2 -Wall -Wextra -Wpedantic -pthread \
    -IANTAGONIZ/Header -IANTAGONIZ/Header/Physics -IThirdParty/Bullet3/src \
    Tests/PhysicsWorker.cpp ANTAGONIZ/Source/Physics/CommandQueue.cpp \
    "$endurance_bullet_build/src/BulletDynamics/libBulletDynamics.a" \
    "$endurance_bullet_build/src/BulletCollision/libBulletCollision.a" \
    "$endurance_bullet_build/src/LinearMath/libLinearMath.a" \
    -o "$endurance_test_binary"
"$endurance_test_binary"
