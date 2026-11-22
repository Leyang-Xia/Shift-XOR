#include <cassert>
#include <cstdlib>

int main(int argc, char** argv) {
    assert(argc == 2);
    return std::system(argv[1]);
}
