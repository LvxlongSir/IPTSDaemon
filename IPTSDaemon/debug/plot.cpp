// SPDX-License-Identifier: GPL-2.0-or-later

#include <common/types.hpp>
#include <contacts/finder.hpp>
#include <container/image.hpp>
#include <gfx/visualization.hpp>
#include <ipts/parser.hpp>
#include "dump.hpp"

#include <algorithm>
#include <cairomm/cairomm.h>
#include <exception>
#include <filesystem>
#include <fmt/core.h>
#include <fstream>
#include <iomanip>
#include <ios>
#include <iostream>
#include <memory>
#include <spdlog/spdlog.h>
#include <vector>

namespace iptsd::debug::plot {

static void iptsd_plot_handle_input(const Cairo::RefPtr<Cairo::Context> &cairo, index2_t rsize,
				    gfx::Visualization &vis, contacts::ContactFinder &finder,
				    const ipts::Heatmap &data)
{
	// Make sure that all buffers have the correct size
	finder.resize(index2_t {data.dim.width, data.dim.height});

	// Normalize and invert the heatmap data.
	std::transform(data.data.begin(), data.data.end(), finder.data().begin(), [&](auto v) {
		f32 val = static_cast<f32>(v - data.dim.z_min) /
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

static int main(char *dump_file, char *plot_dir)
{
    std::filesystem::path path {dump_file};
    std::filesystem::path output {plot_dir};

	std::ifstream ifs {};
	ifs.open(path, std::ios::in | std::ios::binary);

	struct debug::iptsd_dump_header header {};

	ifs.read(reinterpret_cast<char *>(&header), sizeof(header));

	char has_meta = 0;
	ifs.read(&has_meta, sizeof(has_meta));

	// Read metadata
	std::optional<IPTSDeviceMetaData> meta = std::nullopt;
	if (has_meta) {
        IPTSDeviceMetaData m {};
		ifs.read(reinterpret_cast<char *>(&m), sizeof(m));
		meta = m;
	}

	spdlog::info("Vendor:       {:04X}", header.vendor);
	spdlog::info("Product:      {:04X}", header.product);

	if (meta.has_value()) {
		const auto &m = meta;
		auto &t = m->transform;
		auto &u = m->unknown2;

		spdlog::info("Metadata:");
		spdlog::info("rows={}, columns={}", m->size.rows, m->size.columns);
		spdlog::info("width={}, height={}", m->size.width, m->size.height);
		spdlog::info("transform=[{},{},{},{},{},{}]", t.xx, t.yx, t.tx, t.xy, t.yy, t.ty);
		spdlog::info("unknown={}, [{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}]",
			     m->unknown1, u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7], u[8],
			     u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
	}

	config::Config config {header.vendor, header.product, meta};

	// Check if a config was found
	if (config.width == 0 || config.height == 0)
		throw std::runtime_error("No display config for this device was found!");

	gfx::Visualization vis {config};
	contacts::ContactFinder finder {config.contacts()};

	index2_t rsize {};
	f64 aspect = config.width / config.height;

	// Determine the output resolution
	rsize.y = 1000;
	rsize.x = gsl::narrow<int>(std::round(aspect * rsize.y));

	// Create a texture for drawing
	auto drawtex = Cairo::ImageSurface::create(Cairo::ImageSurface::Format::ARGB32, rsize.x, rsize.y);
	auto cairo = Cairo::Context::create(drawtex);

	ipts::Parser parser {};
	parser.on_heatmap = [&](const auto &data) {
		iptsd_plot_handle_input(cairo, rsize, vis, finder, data);
	};

	std::filesystem::create_directories(output);

	u32 i = 0;
	while (ifs.peek() != EOF) {
		try {
			ssize_t size = 0;
			ifs.read(reinterpret_cast<char *>(&size), sizeof(size));
            
            std::vector<u8> buffer(size);
			ifs.read(reinterpret_cast<char *>(buffer.data()), gsl::narrow<std::streamsize>(size));

            auto buf = gsl::span<u8>(buffer.data(), size);
			parser.parse(buf);

			// Save the texture to a png file
			drawtex->write_to_png(output / fmt::format("{:05}.png", i++));
		} catch (std::exception &e) {
			spdlog::warn(e.what());
			continue;
		}
	}

	return 0;
}

} // namespace iptsd::debug::plot

int main(int argc, char *argv[])
{
    if (argc != 3)
        return -1;
    
	spdlog::set_pattern("[%X.%e] [%^%l%$] %v");

	try {
		return iptsd::debug::plot::main(argv[1], argv[2]);
	} catch (std::exception &e) {
		spdlog::error(e.what());
		return EXIT_FAILURE;
	}
}
