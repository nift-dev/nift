// Minify++ carries an older, private JSON parser. Give its JSON types distinct
// linker names when embedding it with Nift's Jsonic, keeping both source mirrors intact.
#define json nift_minify_json
#include "../minifypp/src/Json.h"
#undef json

namespace json = nift_minify_json;
#include "../minifypp/src/Minify.cpp"
