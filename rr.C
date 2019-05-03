// rr.C
// Copyright (c) 2019 Marc Espie <espie@openbsd.org>
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#include <vector>
#include <string>
#include <algorithm>
#include <random>
#include <iostream>
#include <unistd.h>
#include <cerrno>
#include <regex>
#include <cstring>
#include <charconv>
#include <sys/wait.h>

auto
usage()
{
	std::cerr << "Usage: random_run [-1v] [-n maxargs] [-x regex] cmd [flags --] params...\n";
	exit(1);
}

// XXX quick&dirty reimplementation of a very small part of span
// to be trashed once C++20 is standard
template<typename T>
class minispan
{
	T *first;
	size_t count;
public:
	minispan(T *first_, size_t count_): first{first_}, count{count_}
	{
	}
	auto begin()
	{
		return first;
	}
	auto end()
	{
		return first+count;
	}
};

// since we're not in std, Koenig lookup doesn't apply
// ... so we must declare our own begin/end
template<typename T>
inline auto begin(minispan<T>& s)
{
	return s.begin();
}
template<typename T>
inline auto end(minispan<T>& s)
{
	return s.end();
}

void
system_error(const char *msg)
{
	std::cerr << msg << ": " << strerror(errno) << "\n";
	exit(1);
}

void
really_exec(const std::vector<char *>& v)
{
	execvp(v[0], v.data());
	system_error("execvp");
}

void
deal_with_child(int pid)
{
	int r;
	auto e = waitpid(pid, &r, 0);
	if (e == -1)
		system_error("waitpid");
	if (e != pid) {
		std::cerr << "waitpid exited with " << e << 
		    "(shouldn't happen)\n";
		exit(1);
	}
		
	if (WIFEXITED(r)) {
		auto s = WEXITSTATUS(r);
		if (s != 0) {
			std::cerr << "Command exited with "<< s << "\n";
			exit(1);
		}
	} else {
		auto s = WTERMSIG(r);
		std::cerr << "Command exited on signal #"<< s << "\n";
		exit(1);
	}
}

template<class it>
auto
execp_vector(bool verbose, it a1, it b1, it a2, it b2, 
    const std::vector<std::regex>& x, std::size_t maxargs)
{
	std::vector<char *> v;
	// first push the actual command
	for (auto i = a1; i != b1; ++i)
		v.push_back(*i);

	auto reset = v.size();
	if (maxargs && v.size() >= maxargs) {
		std::cerr << "Can't obey -n" << maxargs << 
		    ", initial command is too long ("
		    << v.size() << " words)\n";
		usage();
	}

	auto i = a2;

	while (true) {
		// then the filtered params (some ?)
		for (;i != b2 && v.size() != maxargs; ++i) {
			bool found = false;
			for (auto& s: x)
				if (std::regex_match(*i, s)) {
					found = true;
					break;
				}
			if (!found)
				v.push_back(*i);
		}
		if (verbose) {
			std::copy(begin(v), end(v), 
			    std::ostream_iterator<std::string>(std::cout, " "));
			std::cout << "\n";
		}
		v.push_back(nullptr);

		
		// we didn't do them all yet */
		if (i != b2) {
			auto k = fork();
			if (k == -1)
				system_error("fork");
			else if (k == 0)
				really_exec(v);
			else
				deal_with_child(k);
			v.resize(reset);
		} else 
			// XXX sneaky end of loop, exec doesn't return
			really_exec(v);
	}
}

auto
find_end(const char *s)
{
	return s + strlen(s);
}

template<typename T>
void
get_int_value(const char *s, T& r)
{
	auto p = find_end(s);
	auto [ptr, e] = std::from_chars(s, p, r);
	if (e != std::errc(0)) {
		std::cerr << "bad numeric value " << s << ": " << 
		    std::make_error_code(e).message() << "\n";
		usage();
	}
	if (ptr != p) {
		std::cerr << "trailing chars after numeric parameter: " << s << "\n";
		usage();
	}
}

int 
main(int argc, char *argv[])
{
	// all option values
	bool justone = false;
	bool verbose = false;
	std::size_t maxargs = 0;
	std::vector<std::regex> exclude;

	for (int ch; (ch = getopt(argc, argv, "v1n:x:")) != -1;)
		switch(ch) {
		case 'v':
			verbose = true;
			break;
		case 'n':
			get_int_value(optarg, maxargs);
			break;
		case '1':
			justone = true;
			break;
		case 'x':
			try {
				exclude.emplace_back(optarg);
			} catch (std::regex_error& e) {
				std::cerr << "bad regex " << optarg << ": "
				    << e.what() << "\n";
				usage();
			}

			break;
		default:
			usage();
		}

	argc -= optind;
	argv += optind;
	if (argc == 0)
		usage();

	auto v = minispan(argv, argc);

	auto start = begin(v);

	// first parameter is always the actual
	// program name
	auto start_parm = start+1;
	auto end_start = start_parm;

	// try to figure out option end
	auto it = start_parm;

	while (it != end(v)) {
		if (strcmp(*it, "--") == 0)
			break;
		++it;
	}
	if (it == end(v)) {
		it = start_parm;
	} else {
		end_start = it;
		++it;
	}

	// this is the random part
	std::random_device rd;
	std::mt19937 g(rd());
	shuffle(it, end(v), g);
	auto end_it = end(v);
	if (justone)
		end_it = it+1;

	execp_vector(verbose, start, end_start, it, end_it, exclude, maxargs);
	exit(1);
}
