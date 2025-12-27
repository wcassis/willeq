#pragma once

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

struct FileContentsResult {
	std::string contents;
	std::string error;
};

class File {
public:
	static bool Exists(const std::string &name);
	static void Makedir(const std::string& directory_name);
	static FileContentsResult GetContents(const std::string &file_name);
	static std::string GetCwd();
};

bool Exists(const std::string& name);
