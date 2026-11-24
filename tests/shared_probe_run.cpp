// neost-shared-probe-run — appelle la bibliothèque partagée neost-shared-probe
// (cf. shared_probe.cpp). Code de sortie 0 si le cœur a tourné et sérialisé.
#include <cstdio>

extern "C" long neost_shared_probe(void);

int main() {
    const long n = neost_shared_probe();
    std::printf("[shared-probe] %s (state %ld bytes)\n", n > 0 ? "ok" : "FAIL", n);
    return n > 0 ? 0 : 1;
}
