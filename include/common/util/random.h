#pragma once

#include <cstdint>
#include <limits>
#include <random>
#include <utility>
#include <algorithm>
#include <iterator>
#include <type_traits>

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

		// AKA old MakeRandomFloat
		double Real(double low, double high)
		{
			if (low > high)
				std::swap(low, high);
			return real_dist(m_gen, real_param_t(low, high)); // [low, high)
		}

		// example Roll(50) would have a 50% success rate
		// Roll(100) 100%, etc
		// valid values 0-100 (well, higher works too but ...)
		bool Roll(const int required)
		{
			return Int(0, 99) < required;
		}

		// valid values 0.0 - 1.0
		bool Roll(const double required)
		{
			return Real(0.0, 1.0) <= required;
		}

		// same range as client's roll0
		// This is their main high level RNG function
		int Roll0(int max)
		{
			if (max - 1 > 0)
				return Int(0, max - 1);
			return 0;
		}

		// std::shuffle requires a RNG engine passed to it, so lets provide a wrapper to use our engine
		template<typename RandomAccessIterator>
		void Shuffle(RandomAccessIterator first, RandomAccessIterator last)
		{
			static_assert(std::is_same<std::random_access_iterator_tag,
					typename std::iterator_traits<RandomAccessIterator>::iterator_category>::value,
					"EQ::Random::Shuffle requires random access iterators");
			std::shuffle(first, last, m_gen);
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
		typedef std::uniform_real_distribution<double>::param_type real_param_t;
		std::uniform_real_distribution<double> real_dist;
		std::mt19937 m_gen;
	};
}
