#ifndef ca_primitives_config_sanitizer_hpp
#define ca_primitives_config_sanitizer_hpp

#include "1-primitives/config/compiler.hpp"


// -- SANITIZER --------------------------------------------------------------

/* @DOC 1 if the code is built with the sanitizer `name` (`address`: -fsanitize=address), else 0. */
#define ca_sanitizer(name) (ca_sanitizer_impl_##name())

#if defined(__SANITIZE_ADDRESS__)
#	define ca_sanitizer_impl_address() 1
#elif defined(__has_feature)
#	if __has_feature(address_sanitizer)
#		define ca_sanitizer_impl_address() 1
#	else
#		define ca_sanitizer_impl_address() 0
#	endif
#else
#	define ca_sanitizer_impl_address() 0
#endif


// -- NO SANITIZE ------------------------------------------------------------

/* @DOC An attribute, inside `[[...]]`: the function is not instrumented by the sanitizer `name`
   (`address`, `undefined`, ...). Nothing in a build without it. */
#if ca_compiler(clang)
#	define ca_no_sanitize(name) clang::no_sanitize(#name)
#else
#	define ca_no_sanitize(name) gnu::no_sanitize(#name)
#endif


// -- MEMORY POISONING -------------------------------------------------------

/* @DOC `ca_memory_poisoning`: 1 under ASan unless defined to 0, else 0. ca's own allocators
   (ca::basic_heap, ca::arena) then mark for ASan what they do not hand out: free blocks, the
   room of a chunk past its last block, what a scope's pop released. A use of it is reported
   as a use after poison. */
#if !defined(ca_memory_poisoning)
#	define ca_memory_poisoning ca_sanitizer(address)
#endif

#endif // ca_primitives_config_sanitizer_hpp
