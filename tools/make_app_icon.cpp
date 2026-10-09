// SPDX-License-Identifier: GPL-3.0-or-later
// Convert the trusted application PNG to a Windows ICO using the pinned SDL.
#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>

static void littleEndian(std::ostream& out, unsigned value, int bytes) {
    for (int i = 0; i < bytes; ++i)
        out.put(static_cast<char>((value >> (i * 8)) & 255));
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: make-app-icon INPUT.png OUTPUT_DIRECTORY\n";
        return 1;
    }
    try {
        auto* source = SDL_LoadPNG(argv[1]);
        if (!source)
            throw std::runtime_error(SDL_GetError());
        const std::filesystem::path output(argv[2]);
        std::filesystem::create_directories(output);
        SDL_SetSurfaceBlendMode(source, SDL_BLENDMODE_NONE);
        const unsigned sizes[] = {16, 24, 32, 48, 64, 128, 256};
        std::vector<std::vector<char>> images;
        for (auto size : sizes) {
            auto* scaled = SDL_CreateSurface(size, size, SDL_PIXELFORMAT_RGBA32);
            if (!scaled)
                throw std::runtime_error(SDL_GetError());
            const auto path = output / (std::to_string(size) + ".png");
            if (!SDL_BlitSurfaceScaled(source, nullptr, scaled, nullptr, SDL_SCALEMODE_LINEAR) ||
                !SDL_SavePNG(scaled, path.string().c_str())) {
                SDL_DestroySurface(scaled);
                throw std::runtime_error(SDL_GetError());
            }
            SDL_DestroySurface(scaled);
            std::ifstream file(path, std::ios::binary);
            images.emplace_back(std::istreambuf_iterator<char>(file),
                                std::istreambuf_iterator<char>());
            if (images.back().empty())
                throw std::runtime_error("Cannot read generated PNG");
        }
        SDL_DestroySurface(source);
        std::ofstream ico(output / "fta2.ico", std::ios::binary);
        littleEndian(ico, 0, 2);
        littleEndian(ico, 1, 2);
        littleEndian(ico, images.size(), 2);
        unsigned offset = 6 + 16 * images.size();
        for (unsigned i = 0; i < images.size(); ++i) {
            littleEndian(ico, sizes[i] & 255, 1);
            littleEndian(ico, sizes[i] & 255, 1);
            littleEndian(ico, 0, 2);
            littleEndian(ico, 1, 2);
            littleEndian(ico, 32, 2);
            littleEndian(ico, images[i].size(), 4);
            littleEndian(ico, offset, 4);
            offset += images[i].size();
        }
        for (const auto& image : images)
            ico.write(image.data(), image.size());
        ico.close();
        if (!ico)
            throw std::runtime_error("Cannot write ICO");
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "make-app-icon: " << e.what() << '\n';
        return 1;
    }
}
