#ifndef ca_primitives_config_architecture_hpp
#define ca_primitives_config_architecture_hpp


// -- ARCHITECTURE -----------------------------------------------------------

/* @DOC 1 if the target CPU is `arch` (`x86_64`, `aarch64`, `riscv64`), else 0. Other CPUs build,
   with every one 0. */
#define ca_architecture(arch) (ca_architecture_impl_##arch())

#if defined(__x86_64__)
#	define ca_architecture_impl_x86_64() 1
#else
#	define ca_architecture_impl_x86_64() 0
#endif

#if defined(__aarch64__)
#	define ca_architecture_impl_aarch64() 1
#else
#	define ca_architecture_impl_aarch64() 0
#endif

#if defined(__riscv) && __riscv_xlen == 64
#	define ca_architecture_impl_riscv64() 1
#else
#	define ca_architecture_impl_riscv64() 0
#endif


// -- TARGET FEATURE ---------------------------------------------------------

/* @DOC 1 if the code is built to use the instructions `feature`, else 0. `sse2`: 0 on another
   CPU, and on x86-64 under -mgeneral-regs-only or -mno-sse. `floats`, floating-point
   registers: 0 only in an integer-only build (x86-64 without SSE2, aarch64 under
   -mgeneral-regs-only, riscv64 without F), 1 on another CPU. */
#define ca_target_feature(feature) (ca_target_feature_impl_##feature())

#if defined(__SSE2__)
#	define ca_target_feature_impl_sse2() 1
#else
#	define ca_target_feature_impl_sse2() 0
#endif

#if defined(__x86_64__) && !defined(__SSE2__)
#	define ca_target_feature_impl_floats() 0
#elif defined(__aarch64__) && !defined(__ARM_FP)
#	define ca_target_feature_impl_floats() 0
#elif defined(__riscv) && !defined(__riscv_flen)
#	define ca_target_feature_impl_floats() 0
#else
#	define ca_target_feature_impl_floats() 1
#endif

#endif // ca_primitives_config_architecture_hpp
