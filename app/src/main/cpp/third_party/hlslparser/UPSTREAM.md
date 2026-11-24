# HLSL parser / GLES translator

- Source: https://github.com/Thekla/hlslparser
- Revision: 4f4aefdc52ec49910a7542da4e772b9a0c246d7b
- License: MIT (see LICENSE).
- Imported only the parser, tree, tokenizer, writer and GLSL generator.
- Import formatting: trailing whitespace removed; code semantics unchanged.
- Mobile patch: stop at EOF while reading a truncated FX compile state.
- Mobile patch: omit declaration qualifiers in GLSL cast constructors, including
  casts of static const initializers (`float(...)`, never `const float(...)`).
- Mobile patch: `POSITION0` is a vertex output position like `POSITION`.
- Mobile patch: `fwidth` intrinsic (GLSL `fwidth`, with GL_OES_standard_derivatives).
- Mobile patch: `Log_SetSink` routes parser and generator errors to a per-thread sink.
- Mobile patch: `distance` intrinsic; pixel-shader `POSITION`/`POSITION0` inputs read `gl_FragCoord`.
- Mobile patch: a float/half overload tie picks float (`min(19, i)`); `rsqrt` emits `inversesqrt`.
- Mobile patch: GLSL ES 1.00 initialised arrays are declared bare and filled element by element (no array constructors).
- Mobile patch: an explicit cast to another type is still converted to the parameter type.
- GLSL compilation is validated by the GLES driver. Unsupported input fails;
  successful parsing alone is never treated as a usable shader.
