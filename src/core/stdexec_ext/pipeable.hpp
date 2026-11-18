#pragma once

#include <stdexec/execution.hpp>

#include <utility>

namespace redis::stdexec_ext {

/**
 * \brief An algorithm with everything but its input sender bound, so that it
 * can be written `sender | algorithm(args...)`, as stdexec's own adaptors are.
 *
 * stdexec builds its closures from an internal binder; this is the public
 * stand-in for the adaptors written here.
 *
 * \tparam Apply What makes the sender: called once, with the input sender.
 */
template <class Apply>
struct Pipeable {
	Apply apply;

	/// `sender | closure`: \p closure applied to \p sender.
	template <stdexec::sender Sender>
	friend stdexec::sender auto operator|(Sender &&sender, Pipeable closure) {
		return std::move(closure.apply)(std::forward<Sender>(sender));
	}
};

} // namespace redis::stdexec_ext
