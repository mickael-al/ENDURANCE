# Engine shader compilation

Run with a PIC GLEW object from the project build and desktop OpenGL/EGL:

```sh
EGL_PLATFORM=surfaceless python3 Tests/ShaderCompilation/check_shader_compilation.py \
  --glew-object /tmp/endurance-glew-pic.o
```

The fixture builds the production `ShaderCompilation`, `ComputeShader`,
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
