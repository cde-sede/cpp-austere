// The lexical side of ca::fs::path on arbitrary text: normalizing twice changes nothing, the
// result keeps its invariants (no repeated slash, no trailing one, no "." component), the
// components rebuild it, parent and name rebuild it, stem and suffix rebuild the name,
// with_name(name()) and join(name) of the parent give it back, relative_to(parent) is its name,
// and a joined absolute path replaces the base.
#include "5-collections/fs/path.hpp"
#include "5-collections/string/str.hpp"
#include "fuzz/fuzz.hpp"

namespace {

	auto normalized(const ca::sv::str text) noexcept -> bool {
		if (text.empty())
			return false;
		if (text == ca::sv::str{"."} || text == ca::sv::str{"/"})
			return true;
		if (text[text.size() - 1U] == '/')
			return false;
		for (ca::usz i = 0U; i < text.size(); ++i) {
			const bool starts = i == 0U || text[i - 1U] == '/';
			if (text[i] == '/' && i + 1U < text.size() && text[i + 1U] == '/')
				return false;
			if (starts && text[i] == '.' && (i + 1U == text.size() || text[i + 1U] == '/'))
				return false;
		}
		return true;
	}

	auto check(const ca::fs::path& p) noexcept -> void {
		const ca::sv::str text = p.view();
		fuzz::require(normalized(text), "path not normalized");

		auto again = ca::fs::path::make(text);
		if (!again)
			return;
		fuzz::require(*again == p, "normalizing twice changed the path");
		fuzz::require(static_cast<bool>(again->cleanup()), "path cleanup");

		ca::str rebuilt{};
		for (const ca::sv::str part : p.components()) {
			const bool slash = !rebuilt.empty() && rebuilt.view() != ca::sv::str{"/"};
			if ((slash && !rebuilt.append("/")) || !rebuilt.append(part))
				return;
		}
		fuzz::require(rebuilt.view() == (text == ca::sv::str{"."} ? ca::sv::str{} : text), "components do not rebuild the path");
		fuzz::require(static_cast<bool>(rebuilt.cleanup()), "str cleanup");

		const ca::sv::str name = p.name();
		const ca::sv::str parent = p.parent();
		fuzz::require(p.stem().size() + p.suffix().size() == name.size() && name.starts_with(p.stem()) && name.ends_with(p.suffix()), "stem and suffix do not make the name");
		fuzz::require(p.is_absolute() == text.starts_with("/"), "is_absolute");
		if (name.empty()) {
			fuzz::require(text == ca::sv::str{"."} || text == ca::sv::str{"/"}, "a path without a name");
			return;
		}
		auto base = ca::fs::path::make(parent);
		if (!base)
			return;
		fuzz::require(base->view() == parent, "parent not normalized");
		auto joined = base->join(name);
		if (joined) {
			fuzz::require(*joined == p, "parent joined with name is not the path");
			fuzz::require(static_cast<bool>(joined->cleanup()), "path cleanup");
		}
		auto renamed = p.with_name(name);
		if (renamed) {
			fuzz::require(*renamed == p, "with_name(name()) changed the path");
			fuzz::require(static_cast<bool>(renamed->cleanup()), "path cleanup");
		}
		if (parent != ca::sv::str{"."}) {
			auto below = p.relative_to(*base);
			fuzz::require(below && *below == name, "relative_to(parent) is not the name");
		}
		auto absolute = base->join("/x");
		if (absolute) {
			fuzz::require(absolute->view() == ca::sv::str{"/x"}, "an absolute join did not replace");
			fuzz::require(static_cast<bool>(absolute->cleanup()), "path cleanup");
		}
		fuzz::require(static_cast<bool>(base->cleanup()), "path cleanup");
	}

} // namespace

extern "C" auto LLVMFuzzerTestOneInput(const ca::u8* const data, const ca::usz size) -> int {

	fuzz::input in{data, size};
	const ca::sv::str first = in.text(256U);
	const ca::span<const ca::u8> rest = in.rest();
	const ca::sv::str second{reinterpret_cast<const char*>(rest.data()), rest.size()};
	auto p = ca::fs::path::make(first);
	if (!p)
		return 0;
	check(*p);
	auto joined = p->join(second);
	if (joined) {
		check(*joined);
		fuzz::require(!second.starts_with("/") || joined->view().starts_with("/"), "join with an absolute path is relative");
		fuzz::require(static_cast<bool>(joined->cleanup()), "path cleanup");
	}
	fuzz::require(static_cast<bool>(p->cleanup()), "path cleanup");
	return 0;
}
