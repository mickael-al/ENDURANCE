#include "glcore.hpp"
#include "ShaderBinaryCache.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <system_error>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace Ge::detail
{
    namespace
    {
        namespace fs = std::filesystem;
        using Digest = std::array<std::uint8_t, 32>;
        constexpr std::uint32_t CacheVersion = 1;
        constexpr std::uint64_t MaximumBinaryBytes = 128u * 1024u * 1024u;
        constexpr std::size_t HeaderBytes = 88;
        constexpr char Magic[] = "ENDGLBIN";

        // Streaming SHA-256, with fixed-width arithmetic and no dependencies.
        // Length-framed keys and a separate payload digest distinguish both
        // content revisions and truncated/corrupted cache files.
        class Sha256
        {
        public:
            void update(const void* input, std::size_t count)
            {
                const auto* bytes = static_cast<const std::uint8_t*>(input);
                m_bytes += count;
                while (count)
                {
                    const auto take = (std::min)(count, m_block.size() - m_used);
                    std::memcpy(m_block.data() + m_used, bytes, take);
                    m_used += take; bytes += take; count -= take;
                    if (m_used == m_block.size()) { transform(); m_used = 0; }
                }
            }

            Digest finish()
            {
                const std::uint64_t bits = m_bytes * 8;
                m_block[m_used++] = 0x80;
                if (m_used > 56)
                {
                    std::fill(m_block.begin() + m_used, m_block.end(), 0);
                    transform(); m_used = 0;
                }
                std::fill(m_block.begin() + m_used, m_block.begin() + 56, 0);
                for (unsigned i = 0; i < 8; ++i)
                    m_block[63 - i] = static_cast<std::uint8_t>(bits >> (8 * i));
                transform();
                Digest result{};
                for (unsigned i = 0; i < 8; ++i)
                    for (unsigned b = 0; b < 4; ++b)
                        result[4 * i + b] = static_cast<std::uint8_t>(m_state[i] >> (24 - 8 * b));
                return result;
            }

            void number(std::uint64_t value)
            {
                std::array<std::uint8_t, 8> bytes{};
                for (unsigned i = 0; i < 8; ++i) bytes[i] = static_cast<std::uint8_t>(value >> (8 * i));
                update(bytes.data(), bytes.size());
            }

            void text(const std::string& value)
            {
                number(value.size());
                update(value.data(), value.size());
            }

        private:
            static std::uint32_t rotate(std::uint32_t value, unsigned bits)
            { return (value >> bits) | (value << (32 - bits)); }

            void transform()
            {
                static constexpr std::uint32_t constants[64] = {
                    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
                    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
                    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
                    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
                    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
                    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
                    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
                    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
                };
                std::uint32_t words[64]{};
                for (unsigned i = 0; i < 16; ++i)
                    words[i] = (std::uint32_t(m_block[4*i]) << 24) | (std::uint32_t(m_block[4*i+1]) << 16)
                        | (std::uint32_t(m_block[4*i+2]) << 8) | std::uint32_t(m_block[4*i+3]);
                for (unsigned i = 16; i < 64; ++i)
                {
                    const auto a = words[i-15], b = words[i-2];
                    words[i] = words[i-16] + (rotate(a,7) ^ rotate(a,18) ^ (a >> 3)) + words[i-7]
                        + (rotate(b,17) ^ rotate(b,19) ^ (b >> 10));
                }
                auto a=m_state[0], b=m_state[1], c=m_state[2], d=m_state[3];
                auto e=m_state[4], f=m_state[5], g=m_state[6], h=m_state[7];
                for (unsigned i = 0; i < 64; ++i)
                {
                    const auto first = h + (rotate(e,6) ^ rotate(e,11) ^ rotate(e,25))
                        + ((e & f) ^ (~e & g)) + constants[i] + words[i];
                    const auto second = (rotate(a,2) ^ rotate(a,13) ^ rotate(a,22))
                        + ((a & b) ^ (a & c) ^ (b & c));
                    h=g; g=f; f=e; e=d+first; d=c; c=b; b=a; a=first+second;
                }
                m_state[0]+=a; m_state[1]+=b; m_state[2]+=c; m_state[3]+=d;
                m_state[4]+=e; m_state[5]+=f; m_state[6]+=g; m_state[7]+=h;
            }
            std::array<std::uint32_t, 8> m_state{{0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
                0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u}};
            std::array<std::uint8_t, 64> m_block{};
            std::uint64_t m_bytes = 0;
            std::size_t m_used = 0;
        };

        Digest digest(const void* bytes, std::size_t size)
        { Sha256 hash; hash.update(bytes, size); return hash.finish(); }

        std::string hexadecimal(const Digest& digest)
        {
            static constexpr char digits[] = "0123456789abcdef";
            std::string result(64, '0');
            for (std::size_t i=0; i<digest.size(); ++i)
            { result[2*i]=digits[digest[i]>>4]; result[2*i+1]=digits[digest[i]&15]; }
            return result;
        }

        void encode(std::uint8_t* bytes, std::uint64_t value, unsigned count)
        { for (unsigned i=0; i<count; ++i) bytes[i]=static_cast<std::uint8_t>(value>>(8*i)); }
        std::uint64_t decode(const std::uint8_t* bytes, unsigned count)
        {
            std::uint64_t result=0;
            for (unsigned i=0; i<count; ++i) result|=std::uint64_t(bytes[i])<<(8*i);
            return result;
        }

        fs::path cacheDirectory(const ShaderCacheOptions& options)
        {
            if (!options.directory.empty()) return fs::u8path(options.directory);
#ifdef _WIN32
            if (const auto* value = _wgetenv(L"ENDURANCE_SHADER_CACHE_DIR"))
                if (*value) return fs::path(value);
            if (const auto* value = _wgetenv(L"LOCALAPPDATA"))
                if (*value) return fs::path(value)/L"ENDURANCE"/L"ShaderCache";
#else
            if (const auto* value = std::getenv("ENDURANCE_SHADER_CACHE_DIR"))
                if (*value) return fs::path(value);
            if (const auto* value = std::getenv("XDG_CACHE_HOME"))
                if (*value) return fs::path(value)/"ENDURANCE"/"ShaderCache";
            if (const auto* value = std::getenv("HOME"))
                if (*value) return fs::path(value)/".cache"/"ENDURANCE"/"ShaderCache";
#endif
            return fs::path("Cache")/"ShaderPrograms";
        }

        fs::path temporaryPath(const fs::path& destination)
        {
            static std::atomic<std::uint64_t> next{0};
#ifdef _WIN32
            const auto process = _getpid();
#else
            const auto process = getpid();
#endif
            const auto ticks=std::chrono::steady_clock::now().time_since_epoch().count();
            auto result=destination;
            result += "."+std::to_string(process)+"."+std::to_string(ticks)+"."
                +std::to_string(next.fetch_add(1,std::memory_order_relaxed))+".tmp";
            return result;
        }

        struct RemoveTemporary
        {
            fs::path path;
            ~RemoveTemporary() { std::error_code error; fs::remove(path,error); }
        };
        struct DeleteProgram
        {
            GLuint id = 0;
            ~DeleteProgram() { if (id) glDeleteProgram(id); }
        };
    }

    void ShaderBinaryCache::report(const std::string& message) const
    { if (!m_label.empty()) std::cerr << "[Shader " << m_label << "] cache " << message << std::endl; }

    bool ShaderBinaryCache::supports(unsigned int format) const
    { return std::binary_search(m_formats.begin(),m_formats.end(),format); }

    ShaderBinaryEnvironment::ShaderBinaryEnvironment(bool enabled)
    {
        if (!enabled) return;
        try
        {
            if (!(GLEW_VERSION_4_1 || GLEW_ARB_get_program_binary) || !glProgramBinary
                || !glGetProgramBinary || !glProgramParameteri)
            { unavailable="program binaries unsupported"; return; }
            GLint count=0;
            glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS,&count);
            if (count<=0 || count>4096)
            { unavailable="no usable binary formats"; return; }
            std::vector<GLint> values(static_cast<std::size_t>(count));
            glGetIntegerv(GL_PROGRAM_BINARY_FORMATS,values.data());
            formats.assign(values.begin(),values.end());
            std::sort(formats.begin(),formats.end());
            formats.erase(std::unique(formats.begin(),formats.end()),formats.end());
            const GLenum names[]={GL_VENDOR,GL_RENDERER,GL_VERSION,GL_SHADING_LANGUAGE_VERSION};
            for (std::size_t i=0;i<driver.size();++i)
            {
                const auto* value=glGetString(names[i]);
                if (!value) { unavailable="incomplete driver identity"; return; }
                driver[i]=reinterpret_cast<const char*>(value);
            }
            GLint contextFlags=0, contextProfile=0;
            glGetIntegerv(GL_CONTEXT_FLAGS,&contextFlags);
            if (GLEW_VERSION_3_2) glGetIntegerv(GL_CONTEXT_PROFILE_MASK,&contextProfile);
            flags=static_cast<unsigned int>(contextFlags);
            profile=static_cast<unsigned int>(contextProfile);
            available=true;
        }
        catch (const std::exception& error) { unavailable=error.what(); }
    }

    ShaderBinaryCache::ShaderBinaryCache(const std::vector<ShaderFile>& stages,
        const std::vector<ShaderSource>& sources, const ShaderCacheOptions& options, const std::string& label,
        const ShaderBinaryEnvironment* environment)
        : m_label(label)
    {
        if (!options.enabled) return;
        try
        {
            const ShaderBinaryEnvironment localEnvironment(environment==nullptr);
            const auto& context=environment ? *environment : localEnvironment;
            if (!context.available)
            { report("unavailable: "+context.unavailable+"; compiling sources"); return; }
            m_formats=context.formats;

            Sha256 key;
            key.text("ENDURANCE OpenGL program binary");
            key.number(CacheVersion);
            key.number(sizeof(void*));
            for (const auto& identity : context.driver) key.text(identity);
            key.number(context.flags);
            key.number(context.profile);
            key.number(m_formats.size());
            for (auto format : m_formats) key.number(format);
            key.number(stages.size());
            for (std::size_t i=0; i<stages.size(); ++i)
            {
                key.number(stages[i].stage);
                // Expanded text includes definitions, #version, all include
                // contents and numeric #line IDs. File paths are not executable
                // input; relocating an otherwise identical project is safe.
                key.text(sources[i].text);
            }
            m_key=key.finish();
            m_path=cacheDirectory(options)/(hexadecimal(m_key)+".glbin");
            m_available=true;
        }
        catch (const std::exception& error)
        { report(std::string("unavailable: ")+error.what()+"; compiling sources"); }
    }

    unsigned int ShaderBinaryCache::load() const
    {
        if (!m_available) return 0;
        try
        {
            std::ifstream input(m_path,std::ios::binary|std::ios::ate);
            if (!input) { report("miss "+m_path.generic_u8string()); return 0; }
            const auto length=input.tellg();
            if (length < std::streampos(HeaderBytes) || length > std::streampos(HeaderBytes+MaximumBinaryBytes))
            { report("rejected: invalid file length; compiling sources"); return 0; }
            input.seekg(0);
            std::array<std::uint8_t, HeaderBytes> header{};
            input.read(reinterpret_cast<char*>(header.data()),header.size());
            const auto payloadSize=decode(header.data()+16,8);
            const auto format=static_cast<GLenum>(decode(header.data()+12,4));
            if (!input || std::memcmp(header.data(),Magic,8)!=0 || decode(header.data()+8,4)!=CacheVersion
                || !std::equal(m_key.begin(),m_key.end(),header.begin()+24)
                || !payloadSize || payloadSize>MaximumBinaryBytes
                || payloadSize>static_cast<std::uint64_t>((std::numeric_limits<GLsizei>::max)())
                || length!=std::streampos(HeaderBytes+payloadSize) || !supports(format))
            { report("rejected: invalid header, key or format; compiling sources"); return 0; }
            std::vector<std::uint8_t> payload(static_cast<std::size_t>(payloadSize));
            input.read(reinterpret_cast<char*>(payload.data()),static_cast<std::streamsize>(payload.size()));
            const auto checksum=digest(payload.data(),payload.size());
            if (!input || !std::equal(checksum.begin(),checksum.end(),header.begin()+56))
            { report("rejected: incomplete or corrupt payload; compiling sources"); return 0; }

            DeleteProgram program{glCreateProgram()};
            if (!program.id) { report("rejected: cannot create program; compiling sources"); return 0; }
            const auto start=std::chrono::steady_clock::now();
            glProgramBinary(program.id,format,payload.data(),static_cast<GLsizei>(payload.size()));
            GLint linked=GL_FALSE;
            glGetProgramiv(program.id,GL_LINK_STATUS,&linked);
            const double milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            if (linked!=GL_TRUE)
            { report("rejected by driver; compiling sources"); return 0; }
            report("hit "+m_path.generic_u8string()+" (binary load "+std::to_string(milliseconds)+" ms)");
            const auto id=program.id;
            program.id=0;
            return id;
        }
        catch (const std::exception& error)
        { report(std::string("read failed: ")+error.what()+"; compiling sources"); return 0; }
    }

    void ShaderBinaryCache::store(unsigned int program) const
    {
        if (!m_available) return;
        try
        {
            GLint length=0;
            glGetProgramiv(program,GL_PROGRAM_BINARY_LENGTH,&length);
            if (length<=0 || static_cast<std::uint64_t>(length)>MaximumBinaryBytes)
            { report("not stored: no binary or binary exceeds size limit"); return; }
            std::vector<std::uint8_t> payload(static_cast<std::size_t>(length));
            GLsizei written=0;
            GLenum format=0;
            glGetProgramBinary(program,length,&written,&format,payload.data());
            if (written<=0 || written>length || !supports(format))
            { report("not stored: driver did not return a usable binary"); return; }
            payload.resize(static_cast<std::size_t>(written));
            std::array<std::uint8_t,HeaderBytes> header{};
            std::memcpy(header.data(),Magic,8);
            encode(header.data()+8,CacheVersion,4);
            encode(header.data()+12,format,4);
            encode(header.data()+16,payload.size(),8);
            std::copy(m_key.begin(),m_key.end(),header.begin()+24);
            const auto checksum=digest(payload.data(),payload.size());
            std::copy(checksum.begin(),checksum.end(),header.begin()+56);

            std::error_code error;
            fs::create_directories(m_path.parent_path(),error);
            if (error) { report("not stored: cannot create cache directory: "+error.message()); return; }
            const auto temporary=temporaryPath(m_path);
            RemoveTemporary cleanup{temporary};
            {
                std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
                if (!output) { report("not stored: cache directory is not writable"); return; }
                output.write(reinterpret_cast<const char*>(header.data()),header.size());
                output.write(reinterpret_cast<const char*>(payload.data()),static_cast<std::streamsize>(payload.size()));
                output.close();
                if (!output) { report("not stored: incomplete cache write"); return; }
            }
            fs::rename(temporary,m_path,error);
#ifdef _WIN32
            // Windows rename cannot replace an existing file. Remove only the
            // exact old entry after the replacement has been completely written.
            // A racing reader may miss the cache, but never sees a partial file.
            if (error && fs::is_regular_file(m_path))
            {
                fs::remove(m_path,error);
                if (!error) fs::rename(temporary,m_path,error);
            }
#endif
            if (error) { report("not stored: cannot publish cache file: "+error.message()); return; }
            report("stored "+m_path.generic_u8string()+" ("+std::to_string(payload.size())+" bytes)");
        }
        catch (const std::exception& error)
        { report(std::string("not stored: ")+error.what()); }
    }
}
