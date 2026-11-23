# HLSL parser / GLES translator

- Source: https://github.com/Thekla/hlslparser
- Revision: 4f4aefdc52ec49910a7542da4e772b9a0c246d7b
- License: MIT (see LICENSE).
- Imported only the parser, tree, tokenizer, writer and GLSL generator.
- Import formatting: trailing whitespace removed; code semantics unchanged.
- Mobile patch: stop at EOF while reading a truncated FX compile state.
- Mobile patch: omit declaration qualifiers in GLSL cast constructors, including
  casts of static const initializers (`float(...)`, never `const float(...)`).
- GLSL compilation is validated by the GLES driver. Unsupported input fails;
  successful parsing alone is never treated as a usable shader.
