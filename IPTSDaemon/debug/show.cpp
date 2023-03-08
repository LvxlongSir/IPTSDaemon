// SPDX-License-Identifier: GPL-2.0-or-later

#include <common/types.hpp>
#include <contacts/finder.hpp>
#include <container/image.hpp>
#include <gfx/visualization.hpp>
#include <ipts/device.hpp>
#include <ipts/parser.hpp>

#include <SDL2/SDL.h>
#include <cairomm/cairomm.h>
#include <filesystem>
#include <gsl/gsl>
#include <gsl/span>
#include <spdlog/spdlog.h>
#include <vector>

namespace iptsd::debug::show {

static void iptsd_show_handle_input(const Cairo::RefPtr<Cairo::Context> &cairo, index2_t rsize,
				    gfx::Visualization &vis, contacts::ContactFinder &finder,
				    const ipts::Heatmap &data)
{
	// Make sure that all buffers have the correct size
	finder.resize(index2_t {data.dim.width, data.dim.height});

	// Normalize and invert the heatmap data.
	std::transform(data.data.begin(), data.data.end(), finder.data().begin(), [&](f32 v) {
		const f32 val = (v - static_cast<f32>(data.dim.z_min)) /
				static_cast<f32>(data.dim.z_max - data.dim.z_min);

		return 1.0f - val;
	});

	// Search for a contact
	const std::vector<contacts::Contact> &contacts = finder.search();

	// Draw the raw heatmap
	vis.draw_heatmap(cairo, rsize, finder.data());

	// Draw the contacts
	vis.draw_contacts(cairo, rsize, contacts);
}

static int main()
{
    ipts::Device device {};

    std::optional<IPTSDeviceMetaData> meta = device.meta_data;
	if (meta.has_value()) {
		auto &t = meta->transform;
		auto &u = meta->unknown2;

		spdlog::info("Metadata:");
		spdlog::info("rows={}, columns={}", meta->size.rows, meta->size.columns);
		spdlog::info("width={}, height={}", meta->size.width, meta->size.height);
		spdlog::info("transform=[{},{},{},{},{},{}]", t.xx, t.yx, t.tx, t.xy, t.yy, t.ty);
		spdlog::info("unknown={}, [{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}]",
			     meta->unknown1, u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7],
			     u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
	}

	config::Config config {device.vendor_id, device.product_id, meta};

	// Check if a config was found
	if (config.width == 0 || config.height == 0)
		throw std::runtime_error("No display config for this device was found!");

	gfx::Visualization vis {config};
	contacts::ContactFinder finder {config.contacts()};

	SDL_Init(SDL_INIT_VIDEO);

	SDL_Window *window = nullptr;
	SDL_Renderer *renderer = nullptr;

	// Create an SDL window
	SDL_CreateWindowAndRenderer(0, 0, SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_ALLOW_HIGHDPI, &window, &renderer);

	index2_t rsize {};
	SDL_GetRendererOutputSize(renderer, &rsize.x, &rsize.y);

	// Create a texture that will be rendered later
	SDL_Texture *rendertex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
						   SDL_TEXTUREACCESS_STREAMING, rsize.x, rsize.y);

	// Create a texture for drawing
    const Cairo::RefPtr<Cairo::ImageSurface> drawtex = Cairo::ImageSurface::create(Cairo::ImageSurface::Format::ARGB32, rsize.x, rsize.y);
    const Cairo::RefPtr<Cairo::Context> cairo = Cairo::Context::create(drawtex);

	ipts::Parser parser {};
	parser.on_heatmap = [&](const ipts::Heatmap &data) {
		iptsd_show_handle_input(cairo, rsize, vis, finder, data);
	};

	// Count errors, if we receive 50 continuous errors, chances are pretty good that
	// something is broken beyond repair and the program should exit.
	i32 errors = 0;
	while (true) {
		if (errors >= 50) {
			spdlog::error("Encountered 50 continuous errors, aborting...");
			break;
		}

		SDL_Event event;
		bool quit = false;

		// Check for SDL quit event
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT)
				quit = true;
		}

		if (quit)
			break;

		try {
            gsl::span<u8> buffer = device.read();
            
            device.process_begin();
			parser.parse(buffer);
            device.process_end();

			void *pixels = nullptr;
			int pitch = 0;

			// Copy drawtex to rendertex
			SDL_LockTexture(rendertex, nullptr, &pixels, &pitch);
			std::memcpy(pixels, drawtex->get_data(), rsize.span() * 4L);
			SDL_UnlockTexture(rendertex);

			// Display rendertex
			SDL_RenderClear(renderer);
			SDL_RenderCopy(renderer, rendertex, nullptr, nullptr);
			SDL_RenderPresent(renderer);
		} catch (std::exception &e) {
			spdlog::warn(e.what());
			errors++;
			continue;
		}

		// Reset error count
		errors = 0;
	}

	SDL_DestroyTexture(rendertex);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);

	SDL_Quit();
	return 0;
}

} // namespace iptsd::debug::show

int main(int argc, char *argv[])
{
	spdlog::set_pattern("[%X.%e] [%^%l%$] %v");

	try {
		return iptsd::debug::show::main();
	} catch (std::exception &e) {
		spdlog::error(e.what());
		return EXIT_FAILURE;
	}
}
