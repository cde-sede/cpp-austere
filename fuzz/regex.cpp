// ca::regex on arbitrary patterns, flags and subjects: a pattern regex_pattern::check rejects
// never compiles (one it accepts may still fail, past the size limit), and once compiled every
// call stays consistent with the others: spans inside
// the subject, a fullmatch is a match is a search hit at 0, finditer and findall agree on how
// many matches there are, sub of the whole match changes nothing, split of what never matches
// is the subject. ASan and UBSan watch the Pike VM; the leak checker, every cleanup. Comparing
// results with CPython's re is tooling/fuzz_regex.py (make fuzz-regex).
#include "5-collections/regex/regex.hpp"
#include "5-collections/string/str.hpp"
#include "fuzz/fuzz.hpp"

namespace {

	auto flags_of(const ca::u8 bits) noexcept -> ca::regex_flags {
		ca::regex_flags flags = ca::regex_flags::none;
		if ((bits & 1U) != 0U)
			flags = flags | ca::regex_flags::ignore_case;
		if ((bits & 2U) != 0U)
			flags = flags | ca::regex_flags::multiline;
		if ((bits & 4U) != 0U)
			flags = flags | ca::regex_flags::dot_all;
		if ((bits & 8U) != 0U)
			flags = flags | ca::regex_flags::verbose;
		if ((bits & 16U) != 0U)
			flags = flags | ca::regex_flags::ascii;
		return flags;
	}

	auto spans_inside(const ca::regex_match<>& match, const ca::usz size) noexcept -> bool {
		if (match.start() > match.end() || match.end() > size)
			return false;
		for (ca::usz group = 1U; group <= match.group_count(); ++group) {
			const ca::optional<ca::usz> start = match.start(group);
			const ca::optional<ca::usz> end = match.end(group);
			if (start.has_value() != end.has_value() || (start && (*start > *end || *end > size)))
				return false;
		}
		return true;
	}

	/* Whether found holds a match, checked inside the subject; cleaned up. */
	auto hit(ca::result<ca::optional<ca::regex_match<>>>&& found, const ca::usz size, ca::usz* const start) noexcept -> bool {
		if (!found)
			return false;
		if (!*found)
			return false;
		fuzz::require(spans_inside(**found, size), "match span outside the subject");
		if (start != nullptr)
			*start = (*found)->start();
		fuzz::require(static_cast<bool>((*found)->cleanup()), "match cleanup failed");
		return true;
	}

} // namespace

extern "C" auto LLVMFuzzerTestOneInput(const ca::u8* const data, const ca::usz size) -> int {

	fuzz::input in{data, size};
	const ca::regex_flags flags = flags_of(in.take<ca::u8>());
	const ca::sv::str pattern = in.text(64U);
	const ca::span<const ca::u8> rest = in.rest();
	const ca::sv::str subject{reinterpret_cast<const char*>(rest.data()), rest.size() < 256U ? rest.size() : 256U};

	auto made = ca::regex::make(ca::regex_pattern{pattern}, flags);
	const ca::optional<ca::regex_error> problem = ca::regex_pattern{pattern}.check(flags);
	if (!made)
		return 0;
	fuzz::require(!problem, "make succeeded where check found a problem");
	ca::regex& regex = *made;

	ca::usz searched = 0U;
	const bool full = hit(regex.fullmatch(subject), subject.size(), nullptr);
	const bool matched = hit(regex.match(subject), subject.size(), nullptr);
	const bool found = hit(regex.search(subject), subject.size(), &searched);
	fuzz::require(!full || matched, "fullmatch without match");
	fuzz::require(!matched || (found && searched == 0U), "match without a search hit at 0");
	if (!subject.empty())
		static_cast<void>(hit(regex.search(subject, subject.size() / 2U), subject.size(), nullptr));

	ca::usz iterated = 0U;
	ca::regex::matches_type matches = regex.finditer(subject);
	for (;;) {
		auto next = matches.next();
		if (!next || !*next)
			break;
		fuzz::require(spans_inside(**next, subject.size()), "finditer span outside the subject");
		fuzz::require(static_cast<bool>((*next)->cleanup()), "finditer match cleanup failed");
		++iterated;
	}
	fuzz::require(found == (iterated != 0U), "search and finditer disagree");

	auto all = regex.findall(subject);
	if (all) {
		const ca::usz per = regex.group_count() > 1U ? regex.group_count() : 1U;
		fuzz::require(all->size() == iterated * per, "findall and finditer disagree");
		fuzz::require(static_cast<bool>(all->cleanup()), "findall cleanup failed");
	}

	auto same = regex.sub("\\g<0>", subject, 0U);
	if (same) {
		fuzz::require(same->view() == subject, "sub of the whole match changed the subject");
		fuzz::require(static_cast<bool>(same->cleanup()), "sub cleanup failed");
	}

	auto pieces = regex.split(subject, 0U);
	if (pieces) {
		if (!found) {
			fuzz::require(pieces->size() == 1U && (*pieces)[0U] == subject, "split without a match changed the subject");
		}
		fuzz::require(static_cast<bool>(pieces->cleanup()), "split cleanup failed");
	}
	fuzz::require(static_cast<bool>(regex.cleanup()), "regex cleanup failed");
	return 0;
}
