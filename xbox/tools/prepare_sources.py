#!/usr/bin/env python3
"""Create the Xbox build tree from the upstream Phase Distorter sources.

Upstream files are never modified. This copies cpp/src and cpp/include into
xbox/build/port/ and applies mechanical rewrites so that the exception shim in
xbox/compat/eb_xbox_compat.hpp type-checks:

  catch (...)           -> catch (const ::eb::xbox::CatchAll &eb_catch_N)
  catch (const T &)     -> catch (const T &eb_catch_N)      (unnamed handlers)
  throw;                -> ::eb::xbox::rethrow();

Line numbers are preserved (rewrites never add or remove newlines), so compiler
diagnostics still point at the upstream line.
"""
import argparse
import re
import shutil
from pathlib import Path

CATCH_RE = re.compile(r"\bcatch\s*\(([^()]*)\)")
THROW_RE = re.compile(r"\bthrow\s*;")
FS_RE = re.compile(r"#\s*include\s*<filesystem>")
SDL_RE = re.compile(r"#\s*include\s*[<\"](?:SDL2/)?SDL\.h[>\"]")
# A named handler: `<type> & name` or `<type> name` where the last token is an identifier
# that follows a `&`, `*` or whitespace-separated type.
NAMED_RE = re.compile(r"[&*]\s*[A-Za-z_]\w*\s*$|^\s*(?:const\s+)?[\w:<>,\s]*?\s[A-Za-z_]\w*\s*$")

counter = 0


def rewrite_catch(match: re.Match) -> str:
    global counter
    inner = match.group(1).strip()
    counter += 1
    name = f"eb_catch_{counter}"
    if inner == "...":
        return f"catch (const ::eb::xbox::CatchAll &{name})"
    # `const T &name` / `T name` are already named; `const T &` / `T &` / `T` are not.
    if re.search(r"&\s*[A-Za-z_]\w*\s*$", inner):
        return match.group(0)
    if inner.endswith("&"):
        return f"catch ({inner} {name})"
    # by-value unnamed handler: `catch (T)` -> make it a named const reference
    if re.fullmatch(r"(?:const\s+)?[\w:]+", inner):
        return f"catch (const {inner.replace('const', '').strip()} &{name})"
    return match.group(0)


def process(text: str) -> str:
    text = CATCH_RE.sub(rewrite_catch, text)
    text = THROW_RE.sub("::eb::xbox::rethrow();", text)
    text = FS_RE.sub("#include <eb_xbox_filesystem.hpp>", text)
    text = text.replace("std::filesystem", "eb::xbox::fs")
    text = SDL_RE.sub("#include <eb_xbox_sdl.hpp>", text)
    text = re.sub(r"\bstd::cout\b", "::eb::xbox::cout", text)
    text = re.sub(r"\bstd::cerr\b", "::eb::xbox::cerr", text)
    return text


def core_closure_sources(cpp_root: Path, root_target: str = "eb_core") -> set:
    """Sources of every CMake library reachable from `root_target` via target_link_libraries."""
    texts = [(cpp_root / "CMakeLists.txt").read_text(encoding="utf-8", errors="replace")]
    texts += [p.read_text(encoding="utf-8", errors="replace") for p in sorted((cpp_root / "cmake").glob("*.cmake"))]
    text = "\n".join(texts)

    sources: dict = {}
    for m in re.finditer(r"add_library\(\s*(\w+)\s+(?:STATIC\s+|OBJECT\s+|INTERFACE\s*)?(.*?)\)", text, re.S):
        sources.setdefault(m.group(1), set()).update(re.findall(r"src/[\w/.\-]+\.cpp", m.group(2)))
    for m in re.finditer(r"target_sources\(\s*(\w+)\s+(?:PRIVATE|PUBLIC)\s+(.*?)\)", text, re.S):
        sources.setdefault(m.group(1), set()).update(re.findall(r"src/[\w/.\-]+\.cpp", m.group(2)))
    edges: dict = {}
    for m in re.finditer(r"target_link_libraries\(\s*(\w+)\s+(.*?)\)", text, re.S):
        edges.setdefault(m.group(1), set()).update(re.findall(r"\b(eb_\w+)\b", m.group(2)))

    seen, todo = set(), [root_target]
    while todo:
        name = todo.pop()
        if name not in seen:
            seen.add(name)
            todo += list(edges.get(name, ()))
    if root_target not in sources:
        raise SystemExit(f"cannot find library {root_target} in the upstream CMake files")
    return set().union(*(sources.get(n, set()) for n in seen))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--upstream", type=Path, required=True, help="Phase-Distorter checkout root")
    parser.add_argument("--out", type=Path, required=True, help="output port tree")
    args = parser.parse_args()

    out = args.out
    if out.exists():
        shutil.rmtree(out)
    changed = total = 0
    for sub in ("src", "include"):
        for path in sorted((args.upstream / "cpp" / sub).rglob("*")):
            if not path.is_file():
                continue
            dest = out / sub / path.relative_to(args.upstream / "cpp" / sub)
            dest.parent.mkdir(parents=True, exist_ok=True)
            total += 1
            if path.suffix in {".cpp", ".hpp", ".h", ".inl"}:
                original = path.read_text(encoding="utf-8", errors="surrogateescape")
                rewritten = process(original)
                if rewritten != original:
                    changed += 1
                dest.write_text(rewritten, encoding="utf-8", errors="surrogateescape")
            else:
                shutil.copy2(path, dest)
    # Mirror the pre-generated program and the third-party DSP into the build tree as
    # file symlinks. nxdk writes each .obj next to its source path, so this keeps every
    # build artifact under xbox/build/ and leaves the upstream tree untouched. Relative
    # #includes still resolve because the symlinks sit in a mirrored directory layout.
    linked = 0
    mirrors = [
        (args.upstream / "generated", out.parent / "generated"),
        (args.upstream / "cpp" / "external" / "spc_dsp", out / "external" / "spc_dsp"),
    ]
    for source_root, mirror_root in mirrors:
        if not source_root.is_dir():
            continue
        for path in sorted(source_root.rglob("*")):
            if not path.is_file():
                continue
            link = mirror_root / path.relative_to(source_root)
            link.parent.mkdir(parents=True, exist_ok=True)
            if link.is_symlink() or link.exists():
                link.unlink()
            link.symlink_to(path.resolve())
            linked += 1
    # The core links a closed set of upstream libraries (eb_native_engine, eb_native_story,
    # eb_scene, ...). Compute that closure from the upstream CMake files so the Makefile
    # never drifts from them: every library reachable from eb_core, and its sources.
    native = sorted(core_closure_sources(args.upstream / "cpp"))
    mk = out.parent / "native_engine_sources.mk"
    mk.write_text("EB_NATIVE_SRCS := " + " ".join(f"$(EB_PORT)/{n}" for n in native) + "\n")

    print(f"prepared {total} files into {out} ({changed} rewritten), mirrored {linked} files, "
          f"{len(native)} sources in the eb_core closure")


if __name__ == "__main__":
    main()
