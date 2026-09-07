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

std::vector<std::string> Strings::Split(const std::string &str, const char delim)
{
	std::vector<std::string> ret;
	std::string::size_type   start = 0;
	auto                     end   = str.find(delim);
	while (end != std::string::npos) {
		ret.emplace_back(str, start, end - start);
		start = end + 1;
		end   = str.find(delim, start);
	}
	if (str.length() > start) {
		ret.emplace_back(str, start, str.length() - start);
	}
	return ret;
}

std::vector<std::string> Strings::Split(const std::string &s, const std::string &delimiter)
{
	size_t                   pos_start = 0, pos_end, delim_len = delimiter.length();
	std::string              token;
	std::vector<std::string> res;

	while ((pos_end = s.find(delimiter, pos_start)) != std::string::npos) {
		token     = s.substr(pos_start, pos_end - pos_start);
		pos_start = pos_end + delim_len;
		res.push_back(token);
	}

	res.emplace_back(s.substr(pos_start));
	return res;
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



std::string Strings::Escape(const std::string &s)
{
	std::string ret;

	size_t      sz = s.length();
	for (size_t i  = 0; i < sz; ++i) {
		char c = s[i];
		switch (c) {
			case '\x00':
				ret += "\\x00";
				break;
			case '\n':
				ret += "\\n";
				break;
			case '\r':
				ret += "\\r";
				break;
			case '\\':
				ret += "\\\\";
				break;
			case '\'':
				ret += "\\'";
				break;
			case '\"':
				ret += "\\\"";
				break;
			case '\x1a':
				ret += "\\x1a";
				break;
			default:
				ret.push_back(c);
				break;
		}
	}

	return ret;
}

bool Strings::IsNumber(const std::string &s)
{
	for (char const &c: s) {
		if (c == s[0] && s[0] == '-') {
			continue;
		}
		if (std::isdigit(c) == 0) {
			return false;
		}
	}

	return true;
}

bool Strings::IsFloat(const std::string &s)
{
	char* ptr;
	strtof(s.c_str(), &ptr);
	return (*ptr) == '\0';
}

std::string Strings::Join(const std::vector<std::string> &ar, const std::string &delim)
{
	std::string ret;
	for (size_t i = 0; i < ar.size(); ++i) {
		if (i != 0) {
			ret += delim;
		}
		ret += ar[i];
	}
	return ret;
}

std::string Strings::Join(const std::vector<uint32_t> &ar, const std::string &delim)
{
	std::string ret;
	for (size_t i = 0; i < ar.size(); ++i) {
		if (i != 0) {
			ret += delim;
		}
		ret += std::to_string(ar[i]);
	}
	return ret;
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

std::string Strings::Replace(std::string subject, const std::string &search, const std::string &replace)
{
	size_t pos = 0;
	while ((pos = subject.find(search, pos)) != std::string::npos) {
		subject.replace(pos, search.length(), replace);
		pos += replace.length();
	}
	return subject;
}

std::string Strings::Repeat(std::string s, int n)
{
	std::string s1 = s;
	for (int    i  = 1; i < n; i++) {
		s += s1;
	}
	return s;
}

bool Strings::Contains(std::vector<std::string> container, const std::string& element)
{
	return std::find(container.begin(), container.end(), element) != container.end();
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

const std::string Strings::ToUpper(std::string s)
{
	std::transform(
		s.begin(), s.end(), s.begin(),
		[](unsigned char c) { return ::toupper(c); }
	);
	return s;
}


std::string Strings::NumberToWords(unsigned long long int n)
{
	std::string res;

	res = Strings::ConvertToDigit((n % 100), "");

	if (n > 100 && n % 100) {
		res = "and " + res;
	}

	res = Strings::ConvertToDigit(((n / 100) % 10), "Hundred ") + res;
	res = Strings::ConvertToDigit(((n / 1000) % 100), "Thousand ") + res;
	res = Strings::ConvertToDigit(((n / 100000) % 100), "Lakh, ") + res;
	res = Strings::ConvertToDigit((n / 10000000) % 100, "Crore, ") + res;
	res = Strings::ConvertToDigit((n / 1000000000) % 100, "Billion, ") + res;

	return res;
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

std::string Strings::ConvertToDigit(int n, const std::string& suffix)
{
	if (n == 0) {
		return "";
	}

	if (n > 19) {
		return NUM_TO_ENGLISH_Y[n / 10] + NUM_TO_ENGLISH_X[n % 10] + suffix;
	}
	else {
		return NUM_TO_ENGLISH_X[n] + suffix;
	}
}

bool Strings::BeginsWith(const std::string& subject, const std::string& search)
{
	if (subject.length() < search.length()) {
		return false;
	}
	return subject.compare(0, search.length(), search) == 0;
}

bool Strings::EndsWith(const std::string& subject, const std::string& search)
{
	if (subject.length() < search.length()) {
		return false;
	}
	return subject.compare(subject.length() - search.length(), search.length(), search) == 0;
}

bool Strings::Contains(const std::string& subject, const std::string& search)
{
	if (subject.length() < search.length()) {
		return false;
	}
	return subject.find(search) != std::string::npos;
}




int Strings::ToInt(const std::string &s, int fallback)
{
	if (!Strings::IsNumber(s)) {
		return fallback;
	}

	try {
		return std::stoi(s);
	}
	catch (std::exception &) {
		return fallback;
	}
}




float Strings::ToFloat(const std::string &s, float fallback)
{
	if (!Strings::IsFloat(s)) {
		return fallback;
	}

	try {
		return std::stof(s);
	}
	catch (std::exception &) {
		return fallback;
	}
}

