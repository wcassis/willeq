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
	T ClampUpper(const T &value, const T &upper)
	{
		return std::min(value, upper);
	}
}
