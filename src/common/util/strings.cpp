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

char *strn0cpy(char *dest, const char *source, uint32 size)
{
	if (!dest) {
		return 0;
	}
	if (size == 0 || source == 0) {
		dest[0] = 0;
		return dest;
	}
	strncpy(dest, source, size);
	dest[size - 1] = 0;
	return dest;
}

bool strn0cpyt(char *dest, const char *source, uint32 size)
{
	if (!dest) {
		return 0;
	}
	if (size == 0 || source == 0) {
		dest[0] = 0;
		return false;
	}
	strncpy(dest, source, size);
	dest[size - 1] = 0;
	return (bool) (source[strlen(dest)] == 0);
}

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

uint32 hextoi(const char *num)
{
	if (num == nullptr) {
		return 0;
	}

	int len = strlen(num);
	if (len < 3) {
		return 0;
	}

	if (num[0] != '0' || (num[1] != 'x' && num[1] != 'X')) {
		return 0;
	}

	uint32   ret = 0;
	int      mul = 1;
	for (int i   = len - 1; i >= 2; i--) {
		if (num[i] >= 'A' && num[i] <= 'F') {
			ret += ((num[i] - 'A') + 10) * mul;
		}
		else if (num[i] >= 'a' && num[i] <= 'f') {
			ret += ((num[i] - 'a') + 10) * mul;
		}
		else if (num[i] >= '0' && num[i] <= '9') {
			ret += (num[i] - '0') * mul;
		}
		else {
			return 0;
		}
		mul *= 16;
	}
	return ret;
}

uint64 hextoi64(const char *num)
{
	if (num == nullptr) {
		return 0;
	}

	int len = strlen(num);
	if (len < 3) {
		return 0;
	}

	if (num[0] != '0' || (num[1] != 'x' && num[1] != 'X')) {
		return 0;
	}

	uint64   ret = 0;
	int      mul = 1;
	for (int i   = len - 1; i >= 2; i--) {
		if (num[i] >= 'A' && num[i] <= 'F') {
			ret += ((num[i] - 'A') + 10) * mul;
		}
		else if (num[i] >= 'a' && num[i] <= 'f') {
			ret += ((num[i] - 'a') + 10) * mul;
		}
		else if (num[i] >= '0' && num[i] <= '9') {
			ret += (num[i] - '0') * mul;
		}
		else {
			return 0;
		}
		mul *= 16;
	}
	return ret;
}

bool atobool(const char *iBool)
{
	if (iBool == nullptr) {
		return false;
	}
	if (!strcasecmp(iBool, "true")) {
		return true;
	}
	if (!strcasecmp(iBool, "false")) {
		return false;
	}
	if (!strcasecmp(iBool, "yes")) {
		return true;
	}
	if (!strcasecmp(iBool, "no")) {
		return false;
	}
	if (!strcasecmp(iBool, "on")) {
		return true;
	}
	if (!strcasecmp(iBool, "off")) {
		return false;
	}
	if (!strcasecmp(iBool, "enable")) {
		return true;
	}
	if (!strcasecmp(iBool, "disable")) {
		return false;
	}
	if (!strcasecmp(iBool, "enabled")) {
		return true;
	}
	if (!strcasecmp(iBool, "disabled")) {
		return false;
	}
	if (!strcasecmp(iBool, "y")) {
		return true;
	}
	if (!strcasecmp(iBool, "n")) {
		return false;
	}
	if (Strings::ToInt(iBool)) {
		return true;
	}
	return false;
}

char *CleanMobName(const char *in, char *out)
{
	unsigned i, j;

	for (i = j = 0; i < strlen(in); i++) {
		if (in[i] == '_') {
			out[j++] = ' ';
		}
		else {
			if (isalpha(in[i]) || (in[i] == '`')) {
				out[j++] = in[i];
			}
		}
	}
	out[j] = 0;
	return out;
}

void RemoveApostrophes(std::string &s)
{
	for (unsigned int i = 0; i < s.length(); ++i)
		if (s[i] == '\'') {
			s[i] = '_';
		}
}

char *RemoveApostrophes(const char *s)
{
	auto NewString = new char[strlen(s) + 1];
	strcpy(NewString, s);
	for (unsigned int i = 0; i < strlen(NewString); ++i)
		if (NewString[i] == '\'') {
			NewString[i] = '_';
		}
	return NewString;
}

const char *ConvertArray(int64 input, char *returnchar)
{
	sprintf(returnchar, "%" PRId64, input);
	return returnchar;
}

const char *ConvertArrayF(float input, char *returnchar)
{
	sprintf(returnchar, "%0.2f", input);
	return returnchar;
}

bool isAlphaNumeric(const char *text)
{
	for (unsigned int charIndex = 0; charIndex < strlen(text); charIndex++) {
		if ((text[charIndex] < 'a' || text[charIndex] > 'z') &&
			(text[charIndex] < 'A' || text[charIndex] > 'Z') &&
			(text[charIndex] < '0' || text[charIndex] > '9')) {
			return false;
		}
	}
	return true;
}

std::string FormatName(const std::string &char_name)
{
	std::string formatted(char_name);
	if (!formatted.empty()) {
		std::transform(formatted.begin(), formatted.end(), formatted.begin(), ::tolower);
		formatted[0] = ::toupper(formatted[0]);
	}
	return formatted;
}

const std::string vStringFormat(const char *format, va_list args)
{
	std::string output;
	va_list     tmpargs;

	va_copy(tmpargs, args);
	int characters_used = vsnprintf(nullptr, 0, format, tmpargs);
	va_end(tmpargs);

	if (characters_used > 0) {
		output.resize(characters_used + 1);

		va_copy(tmpargs, args);
		characters_used = vsnprintf(&output[0], output.capacity(), format, tmpargs);
		va_end(tmpargs);

		output.resize(characters_used);

		if (characters_used < 0) {
			output.clear();
		}
	}
	return output;
}

const std::string StringFormat(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	std::string output = vStringFormat(format, args);
	va_end(args);
	return output;
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

std::string Strings::Strings::GetBetween(const std::string &s, std::string start_delim, std::string stop_delim)
{
	if (s.find(start_delim) == std::string::npos && s.find(stop_delim) == std::string::npos) {
		return "";
	}

	auto first_split = Strings::Split(s, start_delim);
	if (!first_split.empty()) {
		std::string remaining_block = first_split[1];
		auto        second_split    = Strings::Split(remaining_block, stop_delim);
		if (!second_split.empty()) {
			std::string between = second_split[0];
			return between;
		}
	}

	return "";
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

std::string Strings::Implode(const std::string& glue, std::vector<std::string> src)
{
	if (src.empty()) {
		return {};
	}

	std::ostringstream                 output;
	std::vector<std::string>::iterator src_iter;

	for (src_iter = src.begin(); src_iter != src.end(); src_iter++) {
		output << *src_iter << glue;
	}

	std::string final_output = output.str();
	final_output.resize(output.str().size() - glue.size());

	return final_output;
}

std::vector<std::string> Strings::Wrap(std::vector<std::string> &src, const std::string& character)
{
	std::vector<std::string> new_vector;
	new_vector.reserve(src.size());

	for (auto &e: src) {
		if (e == "null") {
			new_vector.emplace_back(e);
			continue;
		}
		new_vector.emplace_back(character + e + character);
	}

	return new_vector;
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

const std::string Strings::UcFirst(const std::string& s)
{
	std::string output = s;
	if (!s.empty()) {
		output[0] = static_cast<char>(::toupper(s[0]));
	}
	return output;
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

std::string Strings::SecondsToTime(int duration, bool is_milliseconds)
{
	if (duration <= 0) {
		return "Unknown";
	}

	if (is_milliseconds && duration < 1000) {
		return fmt::format(
			"{} Millisecond{}",
			duration,
			duration != 1 ? "s" : ""
		);
	}

	int timer_length = (
		is_milliseconds ?
			static_cast<int>(std::ceil(static_cast<float>(duration) / 1000.0f)) :
			duration
	);

	int days = int(timer_length / 86400);
	timer_length %= 86400;
	int hours = int(timer_length / 3600);
	timer_length %= 3600;
	int minutes = int(timer_length / 60);
	timer_length %= 60;
	int         seconds       = timer_length;
	std::string time_string   = "Unknown";
	std::string day_string    = (days == 1 ? "Day" : "Days");
	std::string hour_string   = (hours == 1 ? "Hour" : "Hours");
	std::string minute_string = (minutes == 1 ? "Minute" : "Minutes");
	std::string second_string = (seconds == 1 ? "Second" : "Seconds");

	if (days && hours && minutes && seconds) {
		time_string = fmt::format("{} {}, {} {}, {} {}, and {} {}", days, day_string, hours, hour_string, minutes, minute_string, seconds, second_string);
	}
	else if (days && hours && minutes && !seconds) {
		time_string = fmt::format("{} {}, {} {}, and {} {}", days, day_string, hours, hour_string, minutes, minute_string);
	}
	else if (days && hours && !minutes && seconds) {
		time_string = fmt::format("{} {}, {} {}, and {} {}", days, day_string, hours, hour_string, seconds, second_string);
	}
	else if (days && hours && !minutes && !seconds) {
		time_string = fmt::format("{} {} and {} {}", days, day_string, hours, hour_string);
	}
	else if (days && !hours && minutes && seconds) {
		time_string = fmt::format("{} {}, {} {}, and {} {}", days, day_string, minutes, minute_string, seconds, second_string);
	}
	else if (days && !hours && minutes && !seconds) {
		time_string = fmt::format("{} {} and {} {}", days, day_string, minutes, minute_string);
	}
	else if (days && !hours && !minutes && seconds) {
		time_string = fmt::format("{} {} and {} {}", days, day_string, seconds, second_string);
	}
	else if (days && !hours && !minutes && !seconds) {
		time_string = fmt::format("{} {}", days, day_string);
	}
	else if (!days && hours && minutes && seconds) {
		time_string = fmt::format("{} {}, {} {}, and {} {}", hours, hour_string, minutes, minute_string, seconds, second_string);
	}
	else if (!days && hours && minutes && !seconds) {
		time_string = fmt::format("{} {} and {} {}", hours, hour_string, minutes, minute_string);
	}
	else if (!days && hours && !minutes && seconds) {
		time_string = fmt::format("{} {} and {} {}", hours, hour_string, seconds, second_string);
	}
	else if (!days && hours && !minutes && !seconds) {
		time_string = fmt::format("{} {}", hours, hour_string);
	}
	else if (!days && !hours && minutes && seconds) {
		time_string = fmt::format("{} {} and {} {}", minutes, minute_string, seconds, second_string);
	}
	else if (!days && !hours && minutes && !seconds) {
		time_string = fmt::format("{} {}", minutes, minute_string);
	}
	else if (!days && !hours && !minutes && seconds) {
		time_string = fmt::format("{} {}", seconds, second_string);
	}
	return time_string;
}

std::string &Strings::LTrim(std::string &str, std::string_view chars)
{
	str.erase(0, str.find_first_not_of(chars));
	return str;
}

std::string Strings::MillisecondsToTime(int duration)
{
	return SecondsToTime(duration, true);
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

bool Strings::ContainsLower(const std::string& subject, const std::string& search)
{
	if (subject.length() < search.length()) {
		return false;
	}
	return ToLower(subject).find(ToLower(search)) != std::string::npos;
}

uint32 Strings::TimeToSeconds(std::string time_string)
{
	if (time_string.empty()) {
		return 0;
	}

	time_string = Strings::ToLower(time_string);

	if (time_string == "f") {
		return 0;
	}

	std::string time_unit = time_string;

	time_unit.erase(
		remove_if(
			time_unit.begin(),
			time_unit.end(),
			[](char c) {
				return !isdigit(c);
			}
		),
		time_unit.end()
	);

	auto unit = Strings::ToUnsignedInt(time_unit);
	uint32 duration = 0;

	if (Strings::Contains(time_string, "s")) {
		duration = unit;
	} else if (Strings::Contains(time_string, "m")) {
		duration = unit * 60;
	} else if (Strings::Contains(time_string, "h")) {
		duration = unit * 3600;
	} else if (Strings::Contains(time_string, "d")) {
		duration = unit * 86400;
	} else if (Strings::Contains(time_string, "y")) {
		duration = unit * 31556926;
	}

	return duration;
}

bool Strings::ToBool(const std::string& bool_string)
{
	if (
		Strings::Contains(bool_string, "true") ||
		Strings::Contains(bool_string, "y") ||
		Strings::Contains(bool_string, "yes") ||
		Strings::Contains(bool_string, "on") ||
		Strings::Contains(bool_string, "enable") ||
		Strings::Contains(bool_string, "enabled") ||
		(Strings::IsNumber(bool_string) && Strings::ToInt(bool_string))
	) {
		return true;
	}

	return false;
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

int64 Strings::ToBigInt(const std::string &s, int64 fallback)
{
	if (!Strings::IsNumber(s)) {
		return fallback;
	}

	try {
		return std::stoll(s);
	}
	catch (std::exception &) {
		return fallback;
	}
}

uint32 Strings::ToUnsignedInt(const std::string &s, uint32 fallback)
{
	if (!Strings::IsNumber(s)) {
		return fallback;
	}

	try {
		return std::stoul(s);
	}
	catch (std::exception &) {
		return fallback;
	}
}

uint64 Strings::ToUnsignedBigInt(const std::string &s, uint64 fallback)
{
	if (!Strings::IsNumber(s)) {
		return fallback;
	}

	try {
		return std::stoull(s);
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

std::string Strings::RemoveNumbers(std::string s)
{
	int      current = 0;
	for (int i       = 0; i < s.length(); i++) {
		if (!isdigit(s[i])) {
			s[current] = s[i];
			current++;
		}
	}
	return s.substr(0, current);
}
