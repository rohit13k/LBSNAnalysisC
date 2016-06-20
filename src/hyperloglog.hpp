/*
 * hyperloglog.hpp
 *
 *  Created on: Dec 30, 2015
 *      Author: Rohit
 */

#ifndef HYPERLOGLOG_HPP_
#define HYPERLOGLOG_HPP_

/**
 * @file hyperloglog.hpp
 * @brief HyperLogLog cardinality estimator
 * @date Created 2013/3/20
 * @author Hideaki Ohno
 */

#include <vector>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include "murmur3.h"
#include <math.h>

#define HLL_HASH_SEED 313

#if defined(__has_builtin) && (defined(__GNUC__) || defined(__clang__))

#define _GET_CLZ(x, b) (uint8_t)std::min(b, ::__builtin_clz(x)) + 1

#else

inline uint8_t _get_leading_zero_count(uint32_t x, uint8_t b) {

#if defined (_MSC_VER)
	uint32_t leading_zero_len = 32;
	::_BitScanReverse(&leading_zero_len, x);
	--leading_zero_len;
	return std::min(b, (uint8_t)leading_zero_len);
#else
	uint8_t v = 1;
	while (v <= b && !(x & 0x80000000)) {
		v++;
		x <<= 1;
	}
	return v;
#endif

}
#define _GET_CLZ(x, b) _get_leading_zero_count(x, b)
#endif /* defined(__GNUC__) */

namespace hll {

static const double pow_2_32 = 4294967296.0; ///< 2^32
static const double neg_pow_2_32 = -4294967296.0; ///< -(2^32)

/** @class HyperLogLog
 *  @brief Implement of 'HyperLogLog' estimate cardinality algorithm
 */
class HyperLogLog {
public:

	/**
	 * Constructor
	 *
	 * @param[in] b bit width (register size will be 2 to the b power).
	 *            This value must be in the range[4,30].Default value is 4.
	 *
	 * @exception std::invalid_argument the argument is out of range.
	 */
	HyperLogLog(uint8_t b = 8) throw (std::invalid_argument) :
			b_(b), m_(1 << b), M_(m_, 0) {

		if (b < 4 || 30 < b) {
			throw std::invalid_argument(
					"bit width must be in the range [4,30]");
		}

		double alpha;
		switch (m_) {
		case 16:
			alpha = 0.673;
			break;
		case 32:
			alpha = 0.697;
			break;
		case 64:
			alpha = 0.709;
			break;
		default:
			alpha = 0.7213 / (1.0 + 1.079 / m_);
			break;
		}
		alphaMM_ = alpha * m_ * m_;
	}

	/**
	 * Adds element to the estimator
	 *
	 * @param[in] str string to add
	 * @param[in] len length of string
	 */
	void add(const char* str, uint32_t len) {
		uint32_t hash;
		MurmurHash3_x86_32(str, len, HLL_HASH_SEED, (void*) &hash);
		uint32_t index = hash & ((1 << b_) - 1);
		uint8_t rank = _GET_CLZ((hash << b_), 32 - b_);
		if (rank > M_[index]) {
			M_[index] = rank;
		}
	}

	/**
	 * Estimates cardinality value.
	 *
	 * @return Estimated cardinality value.
	 */
	double estimate() const {

		double estimate;
		double sum = 0.0;

		for (uint32_t i = 0; i < m_; i++) {

			sum += 1.0 / (1 << M_[i]);
		}

		estimate = alphaMM_ / sum; // E in the original paper

		if (estimate <= 2.5 * m_) {
			uint32_t zeros = 0;
			for (uint32_t i = 0; i < m_; i++) {
				if (M_[i] == 0) {
					zeros++;
				}
			}
			if (zeros != 0) {
				estimate = m_ * std::log(static_cast<double>(m_) / zeros);
			}
		} else if (estimate > (1.0 / 30.0) * pow_2_32) {
			estimate = neg_pow_2_32 * log(1.0 - (estimate / pow_2_32));
		}
		return round(estimate);
	}

	/**
	 * Merges the estimate from 'other' into this object, returning the estimate of their union.
	 * The number of registers in each must be the same.
	 *
	 * @param[in] other HyperLogLog instance to be merged
	 *
	 * @exception std::invalid_argument number of registers doesn't match.
	 */
	void merge(const HyperLogLog& other) throw (std::invalid_argument) {
		if (m_ != other.m_) {
			std::stringstream ss;
			ss << "number of registers doesn't match: " << m_ << " != "
					<< other.m_;
			throw std::invalid_argument(ss.str().c_str());
		}
		for (uint32_t r = 0; r < m_; ++r) {
			if (M_[r] < other.M_[r]) {
				M_[r] |= other.M_[r];
			}
		}
	}

	/**
	 * Clears all internal registers.
	 */
	void clear() {
		std::fill(M_.begin(), M_.end(), 0);
	}

	void set(uint32_t index, uint8_t value) {
		M_[index] = value;
	}
	uint8_t get(uint32_t index) {
		return M_[index];
	}
	/**
	 * Returns size of register.
	 *
	 * @return Register size
	 */
	uint32_t registerSize() const {
		return m_;
	}

	/**
	 * Exchanges the content of the instance
	 *
	 * @param[in,out] rhs Another HyperLogLog instance
	 */
	void swap(HyperLogLog& rhs) {
		std::swap(b_, rhs.b_);
		std::swap(m_, rhs.m_);
		std::swap(alphaMM_, rhs.alphaMM_);
		M_.swap(rhs.M_);
	}

	void replace(HyperLogLog& rhs) {
		for (uint32_t r = 0; r < m_; ++r) {
			M_[r] = rhs.get(r);
		}
	}
	void minimum(HyperLogLog& rhs) {
		for (uint32_t r = 0; r < m_; ++r) {
			if (M_[r] < rhs.get(r))
				M_[r] = rhs.get(r);
		}
	}

	/**
	 * Dump the current status to a stream
	 *
	 * @param[out] os The output stream where the data is saved
	 *
	 * @exception std::runtime_error When failed to dump.
	 */
	void dump(std::ostream& os) const throw (std::runtime_error) {
		os.write((char*) &b_, sizeof(b_));
		os.write((char*) &M_[0], sizeof(M_[0]) * M_.size());
		if (os.fail()) {
			throw std::runtime_error("Failed to dump");
		}
	}

	/**
	 * Restore the status from a stream
	 *
	 * @param[in] is The input stream where the status is saved
	 *
	 * @exception std::runtime_error When failed to restore.
	 */
	void restore(std::istream& is) throw (std::runtime_error) {
		uint8_t b = 0;
		is.read((char*) &b, sizeof(b));
		HyperLogLog tempHLL(b);
		is.read((char*) &(tempHLL.M_[0]), sizeof(M_[0]) * tempHLL.m_);
		if (is.fail()) {
			throw std::runtime_error("Failed to restore");
		}
		swap(tempHLL);
	}

protected:
	uint8_t b_; ///< register bit width
	uint32_t m_; ///< register size
	double alphaMM_; ///< alpha * m^2
	std::vector<uint8_t> M_; ///< registers

};

} // namespace hll

#endif /* HYPERLOGLOG_HPP_ */
