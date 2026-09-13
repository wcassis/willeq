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
		std::mt19937 m_gen;
	};
}
