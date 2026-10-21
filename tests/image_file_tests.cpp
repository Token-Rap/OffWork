#include "image_file.h"
#include "check.h"

#include <iostream>

int main() {
    using offwork::IsSupportedImageFile;
    for (const auto* path : {L"clock.png", L"clock.jpg", L"clock.jpeg", L"clock.bmp",
                             L"C:\\截图\\打卡.PnG", L"C:/images/clock.JPEG"}) {
        CHECK(IsSupportedImageFile(path));
    }
    for (const auto* path : {L"", L"clock", L"clock.gif", L"clock.webp", L"clock.png.exe",
                             L"clock.png ", L"C:\\images.png\\clock", L"clock.png/",
                             L".png", L"clock.png:stream"}) {
        CHECK(!IsSupportedImageFile(path));
    }
    CHECK(!IsSupportedImageFile(std::wstring_view(L"clock.png\0.exe", 14)));
    std::cout << "Supported image file tests passed.\n";
}
