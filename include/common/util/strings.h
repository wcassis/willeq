#pragma once

#include <charconv>
#include <sstream>
#include <string.h>
#include <string_view>
#include <vector>
#include <cstdarg>
#include <tuple>
#include <type_traits>

#include <fmt/format.h>

#include "types.h"

#ifdef _WINDOWS
#include <ctype.h>
#include <functional>
#include <algorithm>
#endif

namespace detail {
	// template magic to check if std::from_chars floating point functions exist
	template <typename T, typename = void>
	struct has_from_chars_float : std::false_type { };

	template <typename T>
	struct has_from_chars_float < T,
	std::void_t<decltype(std::from_chars(std::declval<const char *>(), std::declval<const char *>(),
					 std::declval<T &>()))>> : std::true_type { };
}; // namespace detail

namespace EQ {
#if defined(__GNUC__) && (__GNUC__ < 11) && !defined(__clang__)
	enum class chars_format {
		scientific = 1, fixed = 2, hex = 4, general = fixed | scientific
	};
#else
	using chars_format = std::chars_format;
#endif
}; // namespace EQ

class Strings {
public:
	static bool Contains(std::vector<std::string> container, const std::string& element);
	static bool Contains(const std::string& subject, const std::string& search);
	static int ToInt(const std::string &s, int fallback = 0);
	static float ToFloat(const std::string &s, float fallback = 0.0f);
	static bool IsNumber(const std::string &s);
	static bool IsFloat(const std::string &s);
	static const std::string ToLower(std::string s);
	static const std::string ToUpper(std::string s);
	static std::string &LTrim(std::string &str, std::string_view chars = "\t\n\v\f\r ");
	static std::string &RTrim(std::string &str, std::string_view chars = "\t\n\v\f\r ");
	static std::string &Trim(std::string &str, const std::string &chars = "\t\n\v\f\r ");
	static std::string Commify(const std::string &number);
	static std::string Commify(uint16 number) { return Strings::Commify(std::to_string(number)); };
	static std::string Commify(uint32 number) { return Strings::Commify(std::to_string(number)); };
	static std::string Commify(uint64 number) { return Strings::Commify(std::to_string(number)); };
	static std::string Commify(int16 number) { return Strings::Commify(std::to_string(number)); };
	static std::string Commify(int32 number) { return Strings::Commify(std::to_string(number)); };
	static std::string Commify(int64 number) { return Strings::Commify(std::to_string(number)); };
	static std::string ConvertToDigit(int n, const std::string& suffix);
	static std::string Escape(const std::string &s);
	static std::string Join(const std::vector<std::string> &ar, const std::string &delim);
	static std::string Join(const std::vector<uint32_t> &ar, const std::string &delim);
	static std::string NumberToWords(unsigned long long int n);
	static std::string Repeat(std::string s, int n);
	static std::string Replace(std::string subject, const std::string &search, const std::string &replace);
	static std::string::size_type SearchDelim(const std::string &haystack, const std::string &needle, const char deliminator = ',');
	static std::vector<std::string> Split(const std::string &s, const char delim = ',');
	static std::vector<std::string> Split(const std::string& s, const std::string& delimiter);
	static void FindReplace(std::string &string_subject, const std::string &search_string, const std::string &replace_string);
	static inline bool EqualFold(const std::string &string_one, const std::string &string_two) { return strcasecmp(string_one.c_str(), string_two.c_str()) == 0; }
	static std::string Random(size_t length);
	static bool BeginsWith(const std::string& subject, const std::string& search);
	static bool EndsWith(const std::string& subject, const std::string& search);

	template<typename T>
	static std::string
	ImplodePair(const std::string &glue, const std::pair<char, char> &encapsulation, const std::vector<T> &src)
	{
		if (src.empty()) {
			return {};
		}
		std::ostringstream oss;
		for (const T &src_iter: src) {
			oss << encapsulation.first << src_iter << encapsulation.second << glue;
		}
		std::string output(oss.str());
		output.resize(output.size() - glue.size());
		return output;
	}

	// basic string_view overloads that just use std stuff since they work!
	template <typename T>
	std::enable_if_t<std::is_floating_point_v<T> && detail::has_from_chars_float<T>::value, std::from_chars_result>
	static from_chars(std::string_view str, T& value, EQ::chars_format fmt = EQ::chars_format::general)
	{
		return std::from_chars(str.data(), str.data() + str.size(), value, fmt);
	}

	template <typename T>
	std::enable_if_t<std::is_integral_v<T>, std::from_chars_result>
	static from_chars(std::string_view str, T& value, int base = 10)
	{
		return std::from_chars(str.data(), str.data() + str.size(), value, base);
	}

	// fallback versions of floating point in case they're not implemented
	template <typename T>
	std::enable_if_t<std::is_floating_point_v<T> && !detail::has_from_chars_float<T>::value && std::is_same_v<T, float>, std::from_chars_result>
	static from_chars(std::string_view str, T& value, EQ::chars_format fmt = EQ::chars_format::general)
	{
		std::from_chars_result res{};
		std::string tmp_str(str.data(), str.size());
		value = strtof(tmp_str.data(), nullptr);
		return res;
	}

	template <typename T>
	std::enable_if_t<std::is_floating_point_v<T> && !detail::has_from_chars_float<T>::value && std::is_same_v<T, double>, std::from_chars_result>
	static from_chars(std::string_view str, T& value, EQ::chars_format fmt = EQ::chars_format::general)
	{
		std::from_chars_result res{};
		std::string tmp_str(str.data(), str.size());
		value = strtod(tmp_str.data(), nullptr);
		return res;
	}
};


// For converstion of numerics into English
const std::string NUM_TO_ENGLISH_X[] = {
	"", "One ", "Two ", "Three ", "Four ",
	"Five ", "Six ", "Seven ", "Eight ", "Nine ", "Ten ", "Eleven ",
	"Twelve ", "Thirteen ", "Fourteen ", "Fifteen ",
	"Sixteen ", "Seventeen ", "Eighteen ", "Nineteen "
};

const std::string NUM_TO_ENGLISH_Y[] = {
	"", "", "Twenty ", "Thirty ", "Forty ",
	"Fifty ", "Sixty ", "Seventy ", "Eighty ", "Ninety "
};

// old c string functions
const char *MakeLowerString(const char *source);
void MakeLowerString(const char *source, char *target);
