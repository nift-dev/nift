#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PREFIX="$ROOT/dist/embed-prefix"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
cat > "$tmp/consumer.c" <<'C'
#include <nift/c_abi.h>
#include <stdio.h>
#include <string.h>
int main(void) {
  if (strcmp(nift_abi_version(), "1.1") != 0) return 2;
  nift_engine *e=nift_engine_new(); nift_script_result *r=NULL; nift_string out={0};
  if (!e || nift_engine_execute(e,"return 40 + 2;",14,"consumer",8,NULL,NULL,0,&r)!=NIFT_OK || !r || !nift_script_result_ok(r)) return 3;
  if (nift_script_result_value_json(r,&out)!=NIFT_OK || out.length!=2 || memcmp(out.data,"42",2)!=0) return 4;
  nift_script_result_free(r); nift_engine_free(e); puts("staged C consumer: PASS"); return 0;
}
C
cc "$tmp/consumer.c" $(pkg-config --cflags --libs nift) -o "$tmp/consumer"
"$tmp/consumer"
cat > "$tmp/consumer.cpp" <<'CPP'
#include <nift/engine.h>
#include <iostream>
int main(){ nift::Engine e; auto r=e.execute("x := 41; return x + 1;"); if(!r.ok() || r.value().json()!="42") return 5; std::cout<<"staged C++ consumer: PASS\n"; }
CPP
c++ -std=c++17 "$tmp/consumer.cpp" $(pkg-config --cflags --libs nift) -o "$tmp/consumer-cpp"
"$tmp/consumer-cpp"
