#include "otaprog.h"

namespace otaprog {
namespace {
volatile int pct = -1;
}

void set(int p) { pct = p; }
int get() { return pct; }

}  // namespace otaprog
