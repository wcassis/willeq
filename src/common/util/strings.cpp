#include "common/util/strings.h"
#include <fmt/format.h>
#include <algorithm>
#include <cctype>
#include <cinttypes>
#include <cstring>
#include <random>

#ifdef _WINDOWS
#include <windows.h>
#define snprintf	_snprintf
#define strncasecmp	_strnicmp
#define strcasecmp  _stricmp
#else
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#endif

#ifndef va_copy
#define va_copy(d,s) ((d) = (s))
#endif

// Legacy C string functions



const char *MakeLowerString(const char *source)
{
	static char str[128];
	if (!source) {
		return nullptr;
	}
	MakeLowerString(source, str);
	return str;
}

void MakeLowerString(const char *source, char *target)
{
	if (!source || !target) {
		*target = 0;
		return;
	}
	while (*source) {
		*target = tolower(*source);
		target++;
		source++;
	}
	*target = 0;
}

// Strings class methods

std::string Strings::Random(size_t length)
{
	static auto &chrs = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

	thread_local static std::mt19937 rg{std::random_device{}()};
	thread_local static std::uniform_int_distribution<std::string::size_type> pick(0, sizeof(chrs) - 2);

	std::string s;
	s.reserve(length);

	while (length--) {
		s += chrs[pick(rg)];
	}

	return s;
}

std::string::size_type
Strings::SearchDelim(const std::string &haystack, const std::string &needle, const char deliminator)
{
	auto pos = haystack.find(needle);
	while (pos != std::string::npos) {
		auto c = haystack[pos + needle.length()];
		if ((c == '\0' || c == deliminator) && (pos == 0 || haystack[pos - 1] == deliminator)) {
			return pos;
		}
		pos = haystack.find(needle, pos + needle.length());
	}
	return std::string::npos;
}

void
Strings::FindReplace(std::string &string_subject, const std::string &search_string, const std::string &replace_string)
{
	if (string_subject.find(search_string) == std::string::npos) {
		return;
	}

	size_t start_pos = 0;
	while ((start_pos = string_subject.find(search_string, start_pos)) != std::string::npos) {
		string_subject.replace(start_pos, search_string.length(), replace_string);
		start_pos += replace_string.length();
	}
}

std::string Strings::Commify(const std::string &number)
{
	std::string temp_string;

	auto string_length = static_cast<int>(number.length());

	int i = 0;
	for (i = string_length - 3; i >= 0; i -= 3) {
		if (i > 0) {
			temp_string = "," + number.substr(static_cast<unsigned long>(i), 3) + temp_string;
		} else {
			temp_string = number.substr(static_cast<unsigned long>(i), 3) + temp_string;
		}
	}

	temp_string = number.substr(0, static_cast<unsigned long>(3 + i)) + temp_string;

	return temp_string;
}

const std::string Strings::ToLower(std::string s)
{
	std::transform(
		s.begin(), s.end(), s.begin(),
		[](unsigned char c) { return ::tolower(c); }
	);
	return s;
}


std::string &Strings::LTrim(std::string &str, std::string_view chars)
{
	str.erase(0, str.find_first_not_of(chars));
	return str;
}


std::string &Strings::RTrim(std::string &str, std::string_view chars)
{
	str.erase(str.find_last_not_of(chars) + 1);
	return str;
}

std::string &Strings::Trim(std::string &str, const std::string &chars)
{
	return LTrim(RTrim(str, chars), chars);
}
