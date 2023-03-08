// SPDX-License-Identifier: GPL-2.0-or-later

#include <common/signal.hpp>
#include <ipts/device.hpp>
#include <ipts/protocol.hpp>
#include "dump.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <gsl/gsl>
#include <iostream>
#include <iterator>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

struct PrettyBuf {
	const u8 *data;
	size_t size;
};

template <> struct fmt::formatter<gsl::span<const u8>> {
	char hexfmt = 'x';
	char prefix = 'n';

	constexpr auto parse(format_parse_context &ctx)
	{
		auto it = ctx.begin(), end = ctx.end();

		while (it != end && *it != '}') {
			if (*it == 'x' || *it == 'X') {
				hexfmt = *it++;
			} else if (*it == 'n' || *it == 'o' || *it == 'O') {
				prefix = *it++;
			} else {
				throw format_error("invalid format");
			}
		}

		return it;
	}

	template <class FormatContext>
	auto format(const gsl::span<const u8> &buf, FormatContext &ctx)
	{
		char const *pfxstr = prefix == 'o' ? "{:04x}: " : "{:04X}: ";
		char const *fmtstr = hexfmt == 'x' ? "{:02x} " : "{:02X} ";

		auto it = ctx.out();
		for (size_t i = 0; i < buf.size(); i += 32) {
			size_t j = 0;

			if (prefix != 'n')
				it = format_to(it, fmt::runtime(pfxstr), i);

			for (; j < 8 && i + j < buf.size(); j++)
				it = format_to(it, fmt::runtime(fmtstr), buf[i + j]);

			it = format_to(it, " ");

			for (; j < 16 && i + j < buf.size(); j++)
				it = format_to(it, fmt::runtime(fmtstr), buf[i + j]);

			it = format_to(it, " ");

			for (; j < 24 && i + j < buf.size(); j++)
				it = format_to(it, fmt::runtime(fmtstr), buf[i + j]);

			it = format_to(it, " ");

			for (; j < 32 && i + j < buf.size(); j++)
				it = format_to(it, fmt::runtime(fmtstr), buf[i + j]);

			it = format_to(it, "\n");
		}

		return format_to(it, "\n");
	}
};

namespace iptsd::debug::dump {

static int main(char *dump_file)
{
    std::filesystem::path filename {dump_file};

	std::atomic_bool should_exit = false;

	const auto _sigterm = common::signal<SIGTERM>([&](int) { should_exit = true; });
	const auto _sigint = common::signal<SIGINT>([&](int) { should_exit = true; });

	std::ofstream file;
	if (!filename.empty()) {
		file.exceptions(std::ios::badbit | std::ios::failbit);
		file.open(filename, std::ios::out | std::ios::binary);
	}

    ipts::Device dev {};

    std::optional<IPTSDeviceMetaData> &meta = dev.meta_data;
	if (file) {
		struct debug::iptsd_dump_header header {};
		header.vendor = dev.vendor_id;
        header.product = dev.product_id;

		file.write(reinterpret_cast<char *>(&header), sizeof(header));
		char has_meta = meta.has_value() ? 1 : 0;
		file.write(&has_meta, sizeof(has_meta));

		if (meta.has_value()) {
			auto m = meta.value();
			file.write(reinterpret_cast<char *>(&m), sizeof(m));
		}
	}

	spdlog::info("Vendor:       {:04X}", dev.vendor_id);
	spdlog::info("Product:      {:04X}", dev.product_id);

	if (meta.has_value()) {
		const auto &t = meta->transform;
		const auto &u = meta->unknown2;

		spdlog::info("Metadata:");
		spdlog::info("rows={}, columns={}", meta->size.rows, meta->size.columns);
		spdlog::info("width={}, height={}", meta->size.width, meta->size.height);
		spdlog::info("transform=[{},{},{},{},{},{}]", t.xx, t.yx, t.tx, t.xy, t.yy, t.ty);
		spdlog::info("unknown={}, [{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}]",
			     meta->unknown1, u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7],
			     u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
	}

	// Count errors, if we receive 50 continuous errors, chances are pretty good that
	// something is broken beyond repair and the program should exit.
	i32 errors = 0;
	while (!should_exit) {
		if (errors >= 50) {
			spdlog::error("Encountered 50 continuous errors, aborting...");
			break;
		}

		try {
            gsl::span<u8> buffer = dev.read();
            const ssize_t size = buffer.size();
            
            dev.process_begin();

			if (file) {
				file.write(reinterpret_cast<const char *>(&size), sizeof(size));
				file.write(reinterpret_cast<char *>(buffer.data()), gsl::narrow<std::streamsize>(size));
			}
            
            const gsl::span<const u8> buf(buffer.data(), size);

			spdlog::info("== Size: {} ==", size);
			spdlog::info("{:ox}", buf);
            
            dev.process_end();
		} catch (std::exception &e) {
			spdlog::warn(e.what());
			errors++;
			continue;
		}

		// Reset error count
		errors = 0;
	}

	return 0;
}

} // namespace iptsd::debug::dump

int main(int argc, char *argv[])
{
    if (argc != 2)
        return -1;
    
	spdlog::set_pattern("[%X.%e] [%^%l%$] %v");

	try {
		return iptsd::debug::dump::main(argv[1]);
	} catch (std::exception &e) {
		spdlog::error(e.what());
		return EXIT_FAILURE;
	}
}
