#pragma once
// Headless GL capture only. SkinnedCloth.cpp and its worker are production code.
#include <atomic>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>
using GLsizei = int;
using GLsizeiptr = std::ptrdiff_t;
using GLintptr = std::ptrdiff_t;
constexpr unsigned GL_SHADER_STORAGE_BUFFER = 1, GL_DYNAMIC_DRAW = 2;
namespace ClothTest {
inline const std::thread::id mainThread = std::this_thread::get_id();
inline std::atomic<unsigned> foreignCalls{0};
inline unsigned next = 1, bound = 0;
inline std::size_t uploads = 0;
inline std::unordered_map<unsigned, std::vector<unsigned char>> buffers;
inline void mainOnly() {
    if (std::this_thread::get_id() != mainThread) {
        foreignCalls.fetch_add(1, std::memory_order_relaxed);
        throw std::runtime_error("worker accessed scene or OpenGL");
    }
}
}
inline void glGenBuffers(GLsizei n, unsigned* ids) {
    ClothTest::mainOnly();
    for (int i=0; i<n; ++i) { ids[i]=ClothTest::next++; ClothTest::buffers[ids[i]]={}; }
}
inline void glBindBuffer(unsigned, unsigned id) { ClothTest::mainOnly(); ClothTest::bound=id; }
inline void glBufferData(unsigned, GLsizeiptr size, const void* source, unsigned) {
    ClothTest::mainOnly();
    if (size < 0) throw std::runtime_error("negative buffer size");
    auto& out=ClothTest::buffers.at(ClothTest::bound); out.resize(std::size_t(size));
    if (source && size) std::memcpy(out.data(),source,std::size_t(size));
}
inline void glBufferSubData(unsigned, GLintptr offset, GLsizeiptr size, const void* source) {
    ClothTest::mainOnly(); auto& out=ClothTest::buffers.at(ClothTest::bound);
    if (offset<0 || size<0 || std::size_t(offset+size)>out.size()) throw std::runtime_error("buffer overflow");
    if (size) std::memcpy(out.data()+offset,source,std::size_t(size));
    ++ClothTest::uploads;
}
inline void glDeleteBuffers(GLsizei n, const unsigned* ids) {
    ClothTest::mainOnly(); for(int i=0;i<n;++i) ClothTest::buffers.erase(ids[i]);
}
