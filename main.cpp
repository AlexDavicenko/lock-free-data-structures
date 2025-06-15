
#include "MPMC.h"
#include "MPSC.h"
#include "SPSC.h"

int main() {
    test_SPSC();
    test_MPSC();
    test_MPMC();
}
