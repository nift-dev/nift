"""setuptools build for the Nift Python binding.

Builds a BINARY wheel AND a self-contained sdist for the nift package. The
native extension (nift/_nift) is built from a `native/` staging subtree that
contains every required canonical source/header (populated by
packaging/stage-release.sh), so both the wheel and the sdist are fully
self-contained under valid relative paths - an sdist consumer never needs the
canonical checkout. The version is the synchronized canonical Nift release
version: NIFT_VERSION is REQUIRED.
"""
import os

from setuptools import Extension, setup

BASE = os.path.dirname(os.path.abspath(__file__))
NATIVE = os.path.join(BASE, "native")

VERSION = os.environ.get("NIFT_VERSION")
if not VERSION:
    raise SystemExit("NIFT_VERSION is required (synchronized canonical Nift release version)")
if not os.path.isdir(os.path.join(NATIVE, "src")):
    raise SystemExit("missing staged native/ subtree; run packaging/stage-release.sh first")

CABI_SOURCES = [
    "native/src/ProjectOwnership.cpp",
    "native/src/embed/Engine.cpp",
    "native/src/embed/Context.cpp",
    "native/src/Value.cpp",
    "native/src/FileSystem.cpp",
    "native/src/JsonFile.cpp",
    "native/src/JsonSchema.cpp",
    "native/minifypp/src/Minify.cpp",
    "native/markuppp/src/Markup.cpp",
    "native/markuppp/src/AsciiDoc.cpp",
    "native/markuppp/src/ReStructuredText.cpp",
    "native/src/Parser.cpp",
    "native/src/ProjectInfo.cpp",
    "native/src/ProjectRead.cpp",
    "native/src/ProjectState.cpp",
    "native/src/WatchList.cpp",
    "native/src/BuildProgress.cpp",
    "native/src/embed/c_abi.cpp",
]
sources = [os.path.join(BASE, s) for s in CABI_SOURCES]
sources.append(os.path.join(BASE, "src", "nift_module.cc"))

MARKUP_C_NAMES = [
    "blocks", "buffer", "cmark", "cmark_ctype", "houdini_href_e",
    "houdini_html_e", "houdini_html_u", "html", "inlines", "iterator",
    "node", "references", "render", "scanners", "utf8",
]
sources.extend(
    os.path.join(BASE, "native", "markuppp", "vendor", "cmark", name + ".c")
    for name in MARKUP_C_NAMES
)

include_dirs = [
    os.path.join(NATIVE, "include"),
    os.path.join(NATIVE, "src"),
    os.path.join(NATIVE, "minifypp", "include"),
    os.path.join(NATIVE, "minifypp", "src"),
    os.path.join(NATIVE, "markuppp", "include"),
    os.path.join(NATIVE, "markuppp", "vendor", "cmark"),
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
