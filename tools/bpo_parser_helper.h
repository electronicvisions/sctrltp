#pragma once

#include <chrono>
#include <iostream>
#include <vector>
#include <boost/program_options.hpp>
#include <boost/spirit/include/qi.hpp>

namespace bpo_parser_helper {

struct duration
{
	std::chrono::steady_clock::duration value;

	friend std::ostream& operator<<(std::ostream& os, duration const& holder)
	{
		auto s = std::chrono::duration_cast<std::chrono::seconds>(holder.value).count();

		if (s >= 60)
			return os << (s / 60) << "m";
		else
			return os << s << "s";
	}
};

void validate(boost::any& v, std::vector<std::string> const& xs, duration*, long)
{
	using namespace std::chrono_literals;
	namespace qi = boost::spirit::qi;
	namespace bpo = boost::program_options;

	bpo::validators::check_first_occurrence(v);
	std::string s(bpo::validators::get_single_string(xs));

	int magnitude;
	std::chrono::steady_clock::duration factor;

	qi::symbols<char, std::chrono::steady_clock::duration> unit;
	unit.add("s", 1s)("m", 1min)("h", 1h);

	if (parse(s.begin(), s.end(), qi::int_ >> unit >> qi::eoi, magnitude, factor))
		v = duration{magnitude * factor};
	else
		throw bpo::invalid_option_value(s);
}

} // namespace bpo_parser_helper
