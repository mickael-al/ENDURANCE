# This directory has its own configuration flags. In particular, MSVC rejects
# /O2 together with /RTC1, so a target-only /O2 override is insufficient.
# Do not cache or propagate these flags: the engine keeps its ordinary Debug
# checks, and both targets still use the same Debug CRT and ABI.
IF(MSVC)
    STRING(REGEX REPLACE "(^|[ \t])[/-]RTC[^ \t]*" "\\1"
        CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    WHILE(CMAKE_CXX_FLAGS_DEBUG MATCHES "(^|[ \t])[/-](Od|Ob0)([ \t]|$)")
        STRING(REGEX REPLACE "(^|[ \t])[/-](Od|Ob0)([ \t]|$)" "\\1\\3"
            CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    ENDWHILE()
    SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /O2 /Ob2")
ELSEIF(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O3 -g")
ENDIF()

ADD_LIBRARY(ANTAGONIZ_CLOTH OBJECT ${ANTAGONIZ_CLOTH_SOURCES})

# Include and define exactly the same engine/dependency headers as ANTAGONIZ.
# The executable consumes only these objects, not a second copy of the sources.
# Referencing its properties at generation also includes usage requirements
# from linked dependencies, while retaining CMake 3.10 compatibility.
TARGET_INCLUDE_DIRECTORIES(ANTAGONIZ_CLOTH PRIVATE
    "$<TARGET_PROPERTY:ANTAGONIZ,INCLUDE_DIRECTORIES>")
TARGET_COMPILE_DEFINITIONS(ANTAGONIZ_CLOTH PRIVATE
    "$<TARGET_PROPERTY:ANTAGONIZ,COMPILE_DEFINITIONS>")
# CMake 3.10 cannot link an OBJECT library. Carry the thread compile flags
# here; ANTAGONIZ links Threads::Threads for the final executable.
TARGET_COMPILE_OPTIONS(ANTAGONIZ_CLOTH PRIVATE
    "$<TARGET_PROPERTY:Threads::Threads,INTERFACE_COMPILE_OPTIONS>")

IF(MSVC)
    TARGET_COMPILE_OPTIONS(ANTAGONIZ_CLOTH PRIVATE /utf-8)
ELSEIF(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # Required in every configuration, even if the engine's release flags
    # disable exceptions and finite-value checks through fast math.
    TARGET_COMPILE_OPTIONS(ANTAGONIZ_CLOTH PRIVATE -fexceptions -fno-fast-math -fno-math-errno)
ENDIF()
