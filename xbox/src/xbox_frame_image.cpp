// FrameImage::write_ppm lives in upstream's frame_presenter.cpp, next to the OpenGL
// presenter, which the Xbox build does not use. Same behaviour, no GL dependency.
#include "eb/frame_presenter.hpp"

#include <fstream>
#include <stdexcept>

namespace eb {

void FrameImage::write_ppm(const std::string &path) const {
    std::ofstream output(path, std::ios::binary);
    output << "P6\n" << width << ' ' << height << "\n255\n";
    output.write(reinterpret_cast<const char *>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    output.close();
    if (!output) throw std::runtime_error("Cannot write screenshot: " + path);
}

} // namespace eb
