# Parametre Repertoire
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/ANTAGONIZ/Header)
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/ANTAGONIZ/Header/Debug)
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/ANTAGONIZ/Header/Setting)

#User
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/ANTAGONIZ/Header/User)
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/ANTAGONIZ/Header/User/Input)
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/ANTAGONIZ/Header/ProjectUser)

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
SET_TARGET_PROPERTIES(ANTAGONIZ PROPERTIES
    OUTPUT_NAME ENDURANCE
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

IF(MSVC)
	TARGET_COMPILE_OPTIONS(ANTAGONIZ PRIVATE /utf-8)
ENDIF()

find_package(OpenGL REQUIRED)

# ENDURANCE is a standalone, offline editor bootstrap.
TARGET_LINK_LIBRARIES(ANTAGONIZ PRIVATE
    Threads::Threads GLEW::GLEW glfw termcolor tinyobjloader OpenFBX stb glm ImGUI
    OpenAL BulletSoftBody BulletInverseDynamics BulletDynamics BulletCollision
    Bullet3Common LinearMath ${OPENGL_LIBRARIES} ${COMPILER_DEPENDENCIES})
