// ca::hashmap and ca::map under one random sequence of set, setdefault, get, pop, popitem,
// pop_first, retain and clear, against a flat array indexed by key: sizes, values and every
// lookup agree after each step; every so often a full walk compares the contents, in key order
// for the B-tree (with floor and ceiling of a random key). Keys come from a range of 2048 so
// that hashmap groups fill and B-tree nodes split and merge.
#include "1-primitives/integers/i64.hpp"
#include "1-primitives/integers/u16.hpp"
#include "5-collections/map/hashmap.hpp"
#include "5-collections/map/map.hpp"
#include "fuzz/fuzz.hpp"

namespace {

	constexpr int KEYS = 2048;

	struct model final {
			bool _present[KEYS];
			ca::i64 _values[KEYS];
			ca::usz _size;
	}; // struct model

	auto lowest(const model& m) noexcept -> int {
		for (int k = 0; k < KEYS; ++k)
			if (m._present[k])
				return k;
		return -1;
	}

	auto highest(const model& m) noexcept -> int {
		for (int k = KEYS - 1; k >= 0; --k)
			if (m._present[k])
				return k;
		return -1;
	}

	auto agree(const model& m, ca::hashmap<int, ca::i64>& hashed, ca::map<int, ca::i64>& ordered) noexcept -> void {
		fuzz::require(hashed.size() == m._size && ordered.size() == m._size, "map size");
		ca::usz seen = 0U;
		for (auto [key, value] : hashed.items()) {
			fuzz::require(key >= 0 && key < KEYS && m._present[key] && m._values[key] == value, "hashmap walk");
			++seen;
		}
		fuzz::require(seen == m._size, "hashmap walk length");
		int previous = -1;
		seen = 0U;
		for (auto [key, value] : ordered.items()) {
			fuzz::require(key > previous && key < KEYS && m._present[key] && m._values[key] == value, "map walk out of order");
			previous = key;
			++seen;
		}
		fuzz::require(seen == m._size, "map walk length");
	}

	auto bounds(const model& m, ca::map<int, ca::i64>& ordered, const int probe) noexcept -> void {
		int below = -1;
		for (int k = probe; k >= 0; --k)
			if (m._present[k]) {
				below = k;
				break;
			}
		int above = -1;
		for (int k = probe; k < KEYS; ++k)
			if (m._present[k]) {
				above = k;
				break;
			}
		const auto floor = ordered.floor(probe);
		const auto ceiling = ordered.ceiling(probe);
		fuzz::require(floor.has_value() == (below >= 0) && (!floor || floor->key == below), "map floor");
		fuzz::require(ceiling.has_value() == (above >= 0) && (!ceiling || ceiling->key == above), "map ceiling");
		const auto first = ordered.first();
		const auto last = ordered.last();
		fuzz::require(first.has_value() == (m._size != 0U) && (!first || first->key == lowest(m)), "map first");
		fuzz::require(last.has_value() == (m._size != 0U) && (!last || last->key == highest(m)), "map last");
	}

} // namespace

extern "C" auto LLVMFuzzerTestOneInput(const ca::u8* const data, const ca::usz size) -> int {

	static model m;
	m = model{};
	ca::hashmap<int, ca::i64> hashed;
	ca::map<int, ca::i64> ordered;
	fuzz::input in{data, size};
	ca::usz steps = 0U;
	while (!in.empty()) {
		const ca::u8 op = in.take<ca::u8>();
		const int key = static_cast<int>(in.take<ca::u16>() % KEYS);
		const ca::i64 value = static_cast<ca::i64>(steps) * 7 - 3;
		switch (op % 10U) {
		case 0:
		case 1:
		case 2: {
			if (!hashed.set(key, value) || !ordered.set(key, value))
				return 0;
			m._size += m._present[key] ? 0U : 1U;
			m._present[key] = true;
			m._values[key] = value;
			break;
		}
		case 3: {
			auto a = hashed.setdefault(key, value);
			auto b = ordered.setdefault(key, value);
			if (!a || !b)
				return 0;
			if (!m._present[key]) {
				m._present[key] = true;
				m._values[key] = value;
				++m._size;
			}
			fuzz::require(**a == m._values[key] && **b == m._values[key], "setdefault value");
			break;
		}
		case 4:
		case 5: {
			const auto a = hashed.pop(key);
			const auto b = ordered.pop(key);
			fuzz::require(a.has_value() == m._present[key] && b.has_value() == m._present[key], "pop presence");
			if (m._present[key]) {
				fuzz::require(*a == m._values[key] && *b == m._values[key], "pop value");
				m._present[key] = false;
				--m._size;
			}
			break;
		}
		case 6: {
			const auto a = hashed.get(key);
			const auto b = ordered.get(key);
			fuzz::require(a.has_value() == m._present[key] && b.has_value() == m._present[key], "get presence");
			fuzz::require(!a || (*a == m._values[key] && *b == m._values[key]), "get value");
			fuzz::require(hashed.contains(key) == m._present[key] && ordered.contains(key) == m._present[key], "contains");
			break;
		}
		case 7: {
			const auto a = hashed.popitem();
			if (a) {
				fuzz::require(m._present[a->key] && m._values[a->key] == a->value, "hashmap popitem");
				fuzz::require(static_cast<bool>(ordered.pop(a->key)), "popitem key missing from map");
				m._present[a->key] = false;
				--m._size;
			}
			else
				fuzz::require(m._size == 0U, "hashmap popitem on a non-empty map");
			const auto b = (op & 16U) != 0U ? ordered.pop_first() : ordered.popitem();
			if (b) {
				fuzz::require(b->key == ((op & 16U) != 0U ? lowest(m) : highest(m)) && m._values[b->key] == b->value, "map pop_first or popitem");
				fuzz::require(static_cast<bool>(hashed.pop(b->key)), "map key missing from hashmap");
				m._present[b->key] = false;
				--m._size;
			}
			break;
		}
		case 8: {
			const int modulus = 2 + static_cast<int>(op >> 4U);
			const auto test = [modulus](const int k, ca::i64&) noexcept {
				return k % modulus != 0;
			};
			if (!hashed.retain(test) || !ordered.retain(test))
				return 0;
			for (int k = 0; k < KEYS; ++k)
				if (m._present[k] && k % modulus == 0) {
					m._present[k] = false;
					--m._size;
				}
			break;
		}
		default:
			if ((op & 0xF0U) == 0xF0U) {
				if (!hashed.clear() || !ordered.clear())
					return 0;
				m = model{};
			}
			else
				bounds(m, ordered, key);
			break;
		}
		if (++steps % 64U == 0U)
			agree(m, hashed, ordered);
	}
	agree(m, hashed, ordered);
	fuzz::require(static_cast<bool>(hashed.cleanup()) && static_cast<bool>(ordered.cleanup()), "map cleanup");
	return 0;
}
