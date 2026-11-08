/* @DOC Performance of ca::heap.

   Layout: per thread, a heap of 48 size-class queues of 1 MiB spans aligned to their size;
   each span serves one class from its start, its 128-byte header in the last bytes. A block
   finds its span by masking its address: no per-block header, and deallocate needs the size
   only to tell small from large (past 128 KiB, page_allocator).

   | Operation | Time | Notes |
   |---|---|---|
   | allocate, small, block on the free list | O(1) | ~10 instructions inline: TLS load, class from size (shift, or bit_width for >128), pop |
   | deallocate, small, same thread | O(1) | ~15 instructions inline: mask, owner compare, push |
   | allocate, list empty | O(1) amortized | out of line: take remote frees, else carve a page of fresh blocks, else next span or a new one |
   | deallocate, other thread | O(1) | one CAS on the span's list, or on its heap's when the span is full |
   | allocate, deallocate, large | one syscall | mmap / munmap |
   | reallocate | O(1) within a class or large to large | large to large is mremap: no byte copied |

   Space: classes 16 to 128 by 16, then four per doubling, at most 25% over the size asked
   for; blocks are carved a page at a time, so untouched spans cost address space only. An
   emptied span goes back to the system unless it is its class's first; two per heap are kept
   for reuse.

   Threads: no lock and no atomic on a thread's own blocks. A block freed by another thread
   goes to its span's atomic list, collected when the owner runs out; a span marked full sends
   such frees to its heap instead, so that the freeing thread never touches a span the owner may
   release. Heaps are never freed: an exiting thread's heap (thread key, exit stage memory) goes
   to a pool under a spin lock, the next new thread adopts it with its spans. A fork while
   another thread holds that lock leaves the child unable to start a new heap.

   Measurements: see dynamic_array and path; against glibc malloc, 1.25x to 2.5x faster on
   allocation-heavy cases, equal elsewhere. */

#ifndef ca_lifecycle_memory_heap_hpp
#define ca_lifecycle_memory_heap_hpp

#include "4-lifecycle/memory/basic_heap.hpp"
#include "4-lifecycle/memory/heap_geometry.hpp"
#include "4-lifecycle/memory/heap_thread_binding.hpp"
#include "4-lifecycle/memory/page_allocator.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- HEAP ---------------------------------------------------------------

	/* @DOC The general-purpose, stateless allocator, default of allocator-aware types. Sizes up to
	   128 KiB come from size classes (16 to 128 by 16, then four per doubling), each thread
	   serving its own spans of 1 MiB with no lock and no atomic; larger ones from page_allocator.
	   Any thread may free any block: a block freed away from its thread goes back through one
	   atomic exchange. Usable size is the class size. Debug builds check that a block goes back
	   with its size class and alignment, and poison it. */
	using heap = ca::basic_heap<ca::page_allocator, ca::heap_geometry, ca::heap_thread_binding>;

} // namespace ca

#endif // ca_lifecycle_memory_heap_hpp
