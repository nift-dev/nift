"""setuptools build for the Nift Python binding.

Builds a BINARY wheel containing the native extension nift/_nift (the frozen
Nift C ABI compiled into a CPython extension), plus the nift package. The
version is the synchronized canonical Nift release version, taken from
NIFT_VERSION or derived from the CLI; it must never be a placeholder.
"""
import os

from setuptools import Extension, setup

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

VERSION = os.environ.get("NIFT_VERSION", "4.0.7")

# The frozen C ABI implementation (same set the C ABI / bindings link).
CABI_SOURCES = [
    "src/ProjectOwnership.cpp",
    "src/embed/Engine.cpp",
    "src/embed/Context.cpp",
    "src/Value.cpp",
    "src/FileSystem.cpp",
    "src/JsonFile.cpp",
    "src/JsonSchema.cpp",
    "minifypp/src/Minify.cpp",
    "src/Parser.cpp",
    "src/ProjectInfo.cpp",
    "src/ProjectRead.cpp",
    "src/ProjectState.cpp",
    "src/WatchList.cpp",
    "src/BuildProgress.cpp",
    "src/embed/c_abi.cpp",
]

sources = [os.path.join(ROOT, s) for s in CABI_SOURCES]
sources.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), "src", "nift_module.cc"))

include_dirs = [
    os.path.join(ROOT, "include"),
    os.path.join(ROOT, "src"),
    os.path.join(ROOT, "minifypp", "include"),
    os.path.join(ROOT, "minifypp", "src"),
]

setup(
    name="nift",
    version=VERSION,
    description="Nift Embed Python binding: Nift template rendering for Python applications "
                "(CPython C extension over the frozen Nift C ABI).",
    long_description="Nift Embed is optional request-time rendering infrastructure for Nift "
                     "templates. This package exposes the Embedded Nift API and semantics from "
                     "Nift %s." % VERSION,
    packages=["nift"],
    ext_modules=[
        Extension(
            "nift._nift",
            sources=sources,
            include_dirs=include_dirs,
            extra_compile_args=["-std=c++17", "-O2"],
            extra_link_args=["-pthread"],
        )
    ],
    python_requires=">=3.10",
    license="MIT",
)