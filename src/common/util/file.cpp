#include "common/util/file.h"
#include "common/logging.h"

#include <fstream>

#ifdef _WINDOWS
#include <direct.h>
#include <conio.h>
#include <iostream>
#include <dos.h>
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

#include <fmt/format.h>
#include <filesystem>
#include <iostream>
#include <sys/stat.h>
#include <vector>

namespace fs = std::filesystem;

bool File::Exists(const std::string &name)
{
	struct stat sb{};
	if (stat(name.c_str(), &sb) == 0) {
		return true;
	}

	return false;
}

void File::Makedir(const std::string &directory_name)
{
	try {
		fs::create_directory(directory_name);
		fs::permissions(directory_name, fs::perms::owner_all);
	}
	catch (const fs::filesystem_error &ex) {
		LOG_ERROR(MOD_MAIN, "Failed to create directory: {}: {}", directory_name, ex.what());
	}
}

std::string File::GetCwd()
{
	return fs::current_path().string();
}

FileContentsResult File::GetContents(const std::string &file_name)
{
	std::ifstream f(file_name, std::ios::in | std::ios::binary);
	if (!f) {
		return { .error = fmt::format("Couldn't open file [{}]", file_name) };
	}

	constexpr size_t CHUNK_SIZE = 4096;
	std::string lines;
	std::vector<char> buffer(CHUNK_SIZE);

	while (f.read(buffer.data(), CHUNK_SIZE) || f.gcount() > 0) {
		lines.append(buffer.data(), f.gcount());
	}

	return FileContentsResult{
		.contents = lines,
		.error = {}
	};
}

bool Exists(const std::string& name)
{
	return File::Exists(name);
}
