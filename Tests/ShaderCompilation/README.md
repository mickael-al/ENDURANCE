# Engine shader compilation

Run with a PIC GLEW object from the project build and desktop OpenGL/EGL:

```sh
EGL_PLATFORM=surfaceless python3 Tests/ShaderCompilation/check_shader_compilation.py \
  --glew-object /tmp/endurance-glew-pic.o
```

The fixture builds the production `ShaderCompilation`, `ShaderBinaryCache`, `ComputeShader`,
`GraphiquePipeline` and `ShaderPair` translation units. It creates all GLSL in a
temporary directory and does not modify application shaders. Neither `Debug`
nor the ImGui console is linked.

It verifies nested relative includes and source maps, injected definitions,
legacy graphics version fallback, compute version fallback, definitions after a
multiline version comment with same-line GLSL, original diagnostic line numbers, missing and cyclic
includes, compiler diagnostics, graphics link failures, and program ownership.
Successful compute programs write values to a real SSBO; graphics programs
render a checked pixel. `takeProgram()` must leave ownership with the caller
through the wrapper's destruction.

The diagnostic-size case retains real compilation failure and substitutes only
the driver's log queries with a 128 KiB message. It checks full preservation of
the error, including its suffix, without relying on a driver's error-count cap.
This is a correctness test; software EGL timings are not hardware benchmarks.

## Persistent program binaries

```sh
EGL_PLATFORM=surfaceless python3 Tests/ShaderCompilation/check_binary_cache.py \
  --glew-object /tmp/endurance-glew-pic.o
```

This fixture uses real program binaries and real compute/graphics execution.
GLEW entry-point counters verify that a cold build compiles and links, while a
warm hit calls `glProgramBinary` without `glCompileShader` or `glLinkProgram`.
It checks wrapper ownership, `takeProgram()`, default uniform restoration and
reapplication, nested include edits with unchanged size and timestamp, defines,
shader-stage separation, and broken/missing source diagnostics despite an older
valid cache entry. A separate cold process then a fresh warm process share only
the temporary source and disk cache, proving persistence beyond live GL objects.
Python's independent SHA-256 implementation checks the stored payload digest.

Fallback cases cover truncated headers, damaged payloads, wrong keys/versions,
unsupported formats, excessive recorded lengths, a simulated driver rejection,
a different renderer fingerprint, zero supported binary formats and a cache
directory blocked by a regular file. The last case remains effective when the
test runs as root. Fixture-local GL query wrappers simulate the renderer and
format cases; all ordinary queries use the real driver. No elapsed-time limit or
GPU performance claim is derived from these correctness tests.
