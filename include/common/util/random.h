#pragma once

#include <cstdint>
#include <limits>
#include <random>
#include <utility>

namespace EQ {
	class Random {
	public:
		// AKA old MakeRandomInt
		int Int(int low, int high)
		{
			if (low > high)
				std::swap(low, high);
			return int_dist(m_gen, int_param_t(low, high)); // [low, high]
		}

		// Full 32-bit unsigned draw. Int() cannot express this range: its
		// parameters are `int`, so callers passing uint32_t's max got -1 and
		// a collapsed [-1, 0] range (T21 — this is how every Daybreak session
		// ended up with a connect code of either 0 or 0xFFFFFFFF).
		uint32_t UInt32()
		{
			return uint_dist(m_gen);
		}

		void Reseed()
		{
			// We could do the seed_seq thing here too if we need better seeding
			// but that is mostly overkill for us, so just seed once
			std::random_device rd;
			m_gen.seed(rd());
		}

		Random()
		{
			Reseed();
		}

	private:
		typedef std::uniform_int_distribution<int>::param_type int_param_t;
		std::uniform_int_distribution<int> int_dist;
		std::uniform_int_distribution<uint32_t> uint_dist{0u, std::numeric_limits<uint32_t>::max()};
		std::mt19937 m_gen;
	};
}
