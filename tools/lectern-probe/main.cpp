// lectern-probe: prints media metadata as JSON (the same probe the importer uses).

#include "media/HardwareCapabilities.h"
#include "media/MediaProbe.h"

#include <cstdio>
#include <cstring>

using namespace lectern;

int main(int argc, char** argv) {
    if (argc < 2 || std::strcmp(argv[1], "--help") == 0) {
        std::printf("usage: lectern-probe <media file>...\n       lectern-probe --encoders\n");
        return argc < 2 ? 2 : 0;
    }
    media::initializeFFmpeg(LogLevel::Error);
    if (std::strcmp(argv[1], "--encoders") == 0) {
        std::printf("%s\n", media::HardwareCapabilities::probe(true).describe().c_str());
        return 0;
    }
    int status = 0;
    for (int i = 1; i < argc; ++i) {
        auto info = media::probeMedia(argv[i]);
        if (!info) {
            std::fprintf(stderr, "%s: %s\n", argv[i], info.error().toString().c_str());
            status = 1;
            continue;
        }
        std::printf("%s\n", json::dump(media::toJson(*info)).c_str());
    }
    return status;
}
