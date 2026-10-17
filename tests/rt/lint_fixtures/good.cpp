#include "lint_fixtures/good.h"

int good(int x) {
    if (x > 0) { return x; }
    return -x;
}
