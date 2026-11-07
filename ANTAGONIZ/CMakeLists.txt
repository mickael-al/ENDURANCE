# Parametre Repertoire
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/ANTAGONIZ/Header)
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/ANTAGONIZ/Header/Debug)
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/ANTAGONIZ/Header/Setting)

#User
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/ANTAGONIZ/Header/User)
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/ANTAGONIZ/Header/User/Input)
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/ANTAGONIZ/Header/ProjectUser)
IF(ENDURANCE_ENABLE_NETWORK)
    INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/ANTAGONIZ/Header/Network)
ENDIF()

#Render
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/ANTAGONIZ/Header/Rendering)
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/ANTAGONIZ/Header/Rendering/Window)
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/ANTAGONIZ/Header/Rendering/Shape)
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/ANTAGONIZ/Header/Rendering/Shader)
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/ANTAGONIZ/Header/Rendering/Light)
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/ANTAGONIZ/Header/Rendering/ImGUI)
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/ANTAGONIZ/Header/Rendering/ImGUI/ImGuizmo)
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/ANTAGONIZ/Header/Rendering/Camera)
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/ANTAGONIZ/Header/Rendering/Materials)
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/ANTAGONIZ/Header/Rendering/Skybox)

#Miscellaneous
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/ANTAGONIZ/Header/Miscellaneous)
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/ANTAGONIZ/Header/Interface)
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/ANTAGONIZ/Header/Miscellaneous/PathFinding)
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/ANTAGONIZ/Header/Miscellaneous/Goap)
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/ANTAGONIZ/Header/Miscellaneous/MarchingCubes)
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/ANTAGONIZ/Header/Miscellaneous/WaveFunctionCollapse)
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/ANTAGONIZ/Header/Miscellaneous/NeuralNetwork)
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/ANTAGONIZ/Header/Sound)

#Struct
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/ANTAGONIZ/Header/Struct)

#Physics
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/ThirdParty/Bullet3/src)
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/ANTAGONIZ/Header/Physics)


#File
# Regenerate on newly added extension files with CMake 3.12+.
IF(CMAKE_VERSION VERSION_LESS "3.12")
    FILE(GLOB_RECURSE ENGINE_SOURCES *.cpp *.c)
    FILE(GLOB_RECURSE ENGINE_HEADERS *.hpp *.h *.inl)
ELSE()
    FILE(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS *.cpp *.c)
    FILE(GLOB_RECURSE ENGINE_HEADERS CONFIGURE_DEPENDS *.hpp *.h *.inl)
ENDIF()

# The offline configuration does not require Steamworks or libhv headers/libs.
IF(NOT ENDURANCE_ENABLE_NETWORK)
    LIST(FILTER ENGINE_SOURCES EXCLUDE REGEX "/Source/Network/")
ENDIF()

# Keep the cloth hot paths optimized in Debug without changing the CRT,
# symbols, definitions, or optimization of the rest of the engine. Disable the
# option when stepping through the solver or cloth reconstruction itself.
SET(THREADS_PREFER_PTHREAD_FLAG TRUE)
FIND_PACKAGE(Threads REQUIRED)
OPTION(ANTAGONIZ_OPTIMIZE_CLOTH_DEBUG "Optimize cloth hot paths in Debug builds" ON)
SET(ANTAGONIZ_CLOTH_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Physics/ClothSimulation.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Physics/PreparedClothContacts.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Rendering/Shape/SkinnedCloth.cpp")
IF(ANTAGONIZ_OPTIMIZE_CLOTH_DEBUG)
    LIST(REMOVE_ITEM ENGINE_SOURCES ${ANTAGONIZ_CLOTH_SOURCES})
    ADD_SUBDIRECTORY(Cloth)
    LIST(APPEND ENGINE_SOURCES $<TARGET_OBJECTS:ANTAGONIZ_CLOTH>)
ENDIF()
# Cloth validation uses exceptions and finite-value guards even when the
# remaining release build opts into fast math.
IF(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    SET_PROPERTY(SOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/Physics/ClothSimulation.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/Physics/PreparedClothContacts.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/Rendering/Shape/SkinnedCloth.cpp"
        APPEND_STRING PROPERTY COMPILE_FLAGS " -fexceptions -fno-fast-math")
    # -fno-fast-math restores math errno, so this kernel-only override must
    # follow the common finite-value/exception guards. No cloth caller uses errno.
    SET_PROPERTY(SOURCE ${ANTAGONIZ_CLOTH_SOURCES}
        APPEND_STRING PROPERTY COMPILE_FLAGS " -fno-math-errno")
ENDIF()

ADD_EXECUTABLE(ANTAGONIZ ${ENGINE_SOURCES} ${ENGINE_HEADERS})
TARGET_COMPILE_DEFINITIONS(ANTAGONIZ PRIVATE
    ENDURANCE_ENABLE_NETWORK=$<BOOL:${ENDURANCE_ENABLE_NETWORK}>)
SET_TARGET_PROPERTIES(ANTAGONIZ PROPERTIES
    OUTPUT_NAME ENDURANCE
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

IF(MSVC)
	TARGET_COMPILE_OPTIONS(ANTAGONIZ PRIVATE /utf-8)
ENDIF()

find_package(OpenGL REQUIRED)

# Core engine dependencies are available in both network and offline builds.
TARGET_LINK_LIBRARIES(ANTAGONIZ PRIVATE
    Threads::Threads GLEW::GLEW glfw termcolor tinyobjloader OpenFBX stb glm ImGUI
    OpenAL BulletSoftBody BulletInverseDynamics BulletDynamics BulletCollision
    Bullet3Common LinearMath ${OPENGL_LIBRARIES} ${COMPILER_DEPENDENCIES})

IF(ENDURANCE_ENABLE_NETWORK)
    TARGET_LINK_LIBRARIES(ANTAGONIZ PRIVATE Steamworks::Steamworks hv_static)
    TARGET_COMPILE_DEFINITIONS(ANTAGONIZ PRIVATE ENDURANCE_STEAM_APP_ID=${ENDURANCE_STEAM_APP_ID})
    # Ship the runtime next to the executable, including for an up-to-date build.
    ADD_CUSTOM_TARGET(ANTAGONIZ_SteamRuntime
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Steamworks::Steamworks>" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        VERBATIM)
    ADD_DEPENDENCIES(ANTAGONIZ ANTAGONIZ_SteamRuntime)
    IF(UNIX)
        SET_PROPERTY(TARGET ANTAGONIZ APPEND PROPERTY BUILD_RPATH "$ORIGIN")
    ENDIF()
ENDIF()

# Always remove a stale development AppID when switching to Release or offline.
ADD_CUSTOM_TARGET(ANTAGONIZ_SteamAppId
    COMMAND ${CMAKE_COMMAND}
        "-DBUILD_CONFIG=$<CONFIG>"
        "-DDEPLOY_APP_ID=$<AND:$<BOOL:${ENDURANCE_ENABLE_NETWORK}>,$<BOOL:${ENDURANCE_DEPLOY_STEAM_APP_ID}>>"
        "-DSTEAM_APP_ID=${ENDURANCE_STEAM_APP_ID}"
        "-DOUTPUT_DIRECTORY=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        -P "${CMAKE_SOURCE_DIR}/CMake/DeploySteamAppId.cmake"
    VERBATIM)
ADD_DEPENDENCIES(ANTAGONIZ ANTAGONIZ_SteamAppId)
