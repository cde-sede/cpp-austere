// ca::heap under random sequences of allocate, deallocate, reallocate, expand and
// allocate_zeroed, sizes across its classes and its large blocks, alignments up to 4 KiB.
// Every live block holds a pattern of its own, checked before it is freed or moved: a block
// handed out twice, or overwritten by the heap's lists, shows. ASan sees inside the heap
// through its poisoning (ca_memory_poisoning).
#include "4-lifecycle/memory/heap.hpp"
#include "fuzz/fuzz.hpp"

namespace {

	constexpr ca::usz SLOTS = 48U;

	struct live final {
			char* _data;
			ca::usz _size;
			ca::usz _alignment;
			char _tag;
	}; // struct live

	auto size_of(fuzz::input& in) noexcept -> ca::usz {
		switch (in.take<ca::u8>() % 8U) {
		case 0: return in.below(17U);
		case 1: return in.below(257U);
		case 2: return in.below(4097U);
		case 3: return in.below(1U << 17U);
		default: return in.below(1025U);
		}
	}

	auto alignment_of(fuzz::input& in) noexcept -> ca::usz {
		const ca::u8 pick = in.take<ca::u8>();
		return pick < 200U ? 8U : ca::usz{1U} << (pick % 13U);
	}

	auto mark(live& block, const ca::usz from) noexcept -> void {
		for (ca::usz i = from; i < block._size; ++i)
			block._data[i] = static_cast<char>(block._tag + static_cast<char>(i * 31U));
	}

	auto intact(const live& block, const ca::usz count) noexcept -> bool {
		for (ca::usz i = 0U; i < count; ++i)
			if (block._data[i] != static_cast<char>(block._tag + static_cast<char>(i * 31U)))
				return false;
		return true;
	}

	auto take(live& block, const ca::result<ca::allocation>& made, const ca::usz size, const ca::usz alignment, const char tag) noexcept -> bool {
		if (!made)
			return false;
		fuzz::require(made->size() >= size, "allocation smaller than asked");
		fuzz::require(size == 0U || (reinterpret_cast<ca::usz>(made->data()) & (alignment - 1U)) == 0U, "allocation misaligned");
		block = live{static_cast<char*>(made->data()), made->size(), alignment, tag};
		return true;
	}

} // namespace

extern "C" auto LLVMFuzzerTestOneInput(const ca::u8* const data, const ca::usz size) -> int {

	const ca::heap heap{};
	live slots[SLOTS] = {};
	bool used[SLOTS] = {};
	fuzz::input in{data, size};
	char tag = 1;
	while (!in.empty()) {
		const ca::usz at = in.below(SLOTS);
		live& block = slots[at];
		const ca::u8 op = in.take<ca::u8>() % 6U;
		if (!used[at]) {
			const ca::usz wanted = size_of(in);
			const ca::usz alignment = alignment_of(in);
			const bool zeroed = op == 5U;
			const auto made = zeroed ? heap.allocate_zeroed(wanted, alignment) : heap.allocate(wanted, alignment);
			if (take(block, made, wanted, alignment, tag++)) {
				if (zeroed)
					for (ca::usz i = 0U; i < block._size; ++i)
						fuzz::require(block._data[i] == 0, "allocate_zeroed not zeroed");
				mark(block, 0U);
				used[at] = true;
			}
			continue;
		}
		fuzz::require(intact(block, block._size), "live block overwritten");
		if (op <= 2U) {
			heap.deallocate(ca::allocation{block._data, block._size}, block._alignment);
			used[at] = false;
		}
		else if (op == 3U) {
			const ca::usz wanted = size_of(in);
			const auto moved = heap.reallocate(ca::allocation{block._data, block._size}, wanted, block._alignment);
			if (!moved)
				continue;
			live grown{};
			const ca::usz kept = block._size < wanted ? block._size : wanted;
			static_cast<void>(take(grown, moved, wanted, block._alignment, block._tag));
			fuzz::require(intact(grown, kept), "reallocate lost contents");
			block = grown;
			mark(block, kept);
			if (wanted == 0U && block._size == 0U)
				used[at] = false;
		}
		else {
			const ca::usz wanted = size_of(in);
			const auto expanded = heap.expand(ca::allocation{block._data, block._size}, wanted, block._alignment);
			if (expanded) {
				fuzz::require(expanded->data() == block._data && expanded->size() >= wanted, "expand moved or fell short");
				const ca::usz kept = block._size < expanded->size() ? block._size : expanded->size();
				block._size = expanded->size();
				mark(block, kept);
			}
		}
	}
	for (ca::usz i = 0U; i < SLOTS; ++i)
		if (used[i]) {
			fuzz::require(intact(slots[i], slots[i]._size), "live block overwritten");
			heap.deallocate(ca::allocation{slots[i]._data, slots[i]._size}, slots[i]._alignment);
		}
	return 0;
}
