#pragma once

#include <algorithm>
#include <cmath>

namespace EQ {
	template<typename T>
	T Clamp(const T &value, const T &lower, const T &upper)
	{
		return std::max(lower, std::min(value, upper));
	}

	template<typename T>
	T ClampLower(const T &value, const T &lower)
	{
		return std::max(lower, value);
	}

	template<typename T>
	T ClampUpper(const T &value, const T &upper)
	{
		return std::min(value, upper);
	}

	template<typename T>
	bool ValueWithin(const T &value, const T &lower, const T &upper)
	{
		return value >= lower && value <= upper;
	}

	template<typename T1, typename T2, typename T3>
	bool ValueWithin(const T1 &value, const T2 &lower, const T3 &upper)
	{
		return value >= (T1) lower && value <= (T1) upper;
	}
}
