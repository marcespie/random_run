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
#include <filesystem>
#include <fstream>

using std::filesystem::path;
using directory_it = std::filesystem::recursive_directory_iterator;
using std::vector;
using std::regex;
using std::cerr;
using std::cout;
using std::ifstream;

auto
path_vector(char *av[], int ac)
{
	vector<path> result;
	for (int i = 0; i != ac; i++)
		result.emplace_back(av[i]);
	return result;
}


auto
usage()
{
	cerr << "Usage: rr [-1Nrv] [-l file] [-n maxargs] [-o regex] [-x regex] cmd [flags --] params...\n";
	exit(1);
}

void
add_lines(vector<path>& r, const char *fname)
{
	ifstream f;
	f.open(fname);
	if (!f.is_open()) {
		cerr << "failed to open " << fname << "\n";
		usage();
	}
	for (std::string line; getline(f, line); ) 
		r.emplace_back(line);
}

void
system_error(const char *msg)
{
	cerr << msg << ": " << strerror(errno) << "\n";
	exit(1);
}

void
really_exec(const vector<const char *>& v)
{
	execvp(v[0], const_cast<char **>(v.data()));
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
		cerr << "waitpid exited with " << e << 
		    "(shouldn't happen)\n";
		exit(1);
	}
		
	if (WIFEXITED(r)) {
		auto s = WEXITSTATUS(r);
		if (s != 0) {
			cerr << "Command exited with "<< s << "\n";
			exit(1);
		}
	} else {
		auto s = WTERMSIG(r);
		cerr << "Command exited on signal #"<< s << "\n";
		exit(1);
	}
}

bool 
any_match(const char *s, const vector<regex>& x)
{
	for (auto& r: x)
		if (regex_match(s, r)) {
			return true;
		}
	return false;
}

void
may_add(vector<const char *>& v, const char *s,
    const vector<regex>& x, const vector<regex>& o)
{
	if (any_match(s, x))
		return;
	if (o.size() == 0) {
		v.push_back(s);
		return;
	}
	if (any_match(s, x))
		v.push_back(s);
}

template<class it>
auto
execp_vector(bool verbose, it a1, it b1, it a2, it b2, 
    const vector<regex>& x, const vector<regex>& o,
    std::size_t maxargs)
{
	vector<const char *> v;
	// first push the actual command
	for (auto i = a1; i != b1; ++i)
		v.push_back(i->c_str());

	auto reset = v.size();
	if (maxargs && v.size() >= maxargs) {
		cerr << "Can't obey -n" << maxargs << 
		    ", initial command is too long ("
		    << v.size() << " words)\n";
		usage();
	}

	auto i = a2;

	while (true) {
		// then the filtered params (some ?)
		for (;i != b2 && v.size() != maxargs; ++i)
			may_add(v, i->c_str(), x, o);
		if (verbose) {
			copy(begin(v), end(v), 
			    std::ostream_iterator<const char *>(cout, " "));
			cout << "\n";
		}
		v.push_back(nullptr);

		if (i != b2) {
			// we didn't do them all yet */
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
get_integer_value(const char *s, T& r)
{
	auto p = find_end(s);
	auto [ptr, e] = std::from_chars(s, p, r);
	if (e != std::errc(0)) {
		cerr << "bad numeric value " << s << ": " << 
		    make_error_code(e).message() << "\n";
		usage();
	}
	if (ptr != p) {
		cerr << "trailing chars after numeric parameter: " << s << "\n";
		usage();
	}
}

int 
main(int argc, char *argv[])
{
	// all option values
	bool justone = false;
	bool verbose = false;
	bool recursive = false;
	bool randomize = true;
	std::size_t maxargs = 0;
	vector<regex> exclude, only;
	vector<char *> list;

	for (int ch; (ch = getopt(argc, argv, "v1l:rn:No:x:")) != -1;)
		switch(ch) {
		case 'v':
			verbose = true;
			break;
		case 'n':
			get_integer_value(optarg, maxargs);
			break;
		case 'N':
			randomize = false;
			break;
		case 'r':
			recursive = true;
			break;
		case '1':
			justone = true;
			break;
		case 'o':
			try {
				only.emplace_back(optarg);
			} catch (std::regex_error& e) {
				cerr << "bad regex " << optarg << ": "
				    << e.what() << "\n";
				usage();
			}

			break;
		case 'l':
			list.push_back(optarg);
			break;
		case 'x':
			try {
				exclude.emplace_back(optarg);
			} catch (std::regex_error& e) {
				cerr << "bad regex " << optarg << ": "
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

	auto v = path_vector(argv, argc);
	for (auto& filename: list)
		add_lines(v, filename);

	auto start = begin(v);

	// first parameter is always the actual
	// program name
	auto start_parm = start+1;
	auto end_start = start_parm;

	// try to figure out option end
	auto it = start_parm;

	while (it != end(v)) {
		if (strcmp(it->c_str(), "--") == 0)
			break;
		++it;
	}
	if (it == end(v)) {
		it = start_parm;
	} else {
		end_start = it;
		++it;
	}

	auto end_it = end(v);

	vector<path> w;
	if (recursive) {
		for (auto i = it; i != end_it; ++i) {
			if (is_directory(*i)) {
				if (!any_match(i->c_str(), exclude))
					for (auto& p: directory_it{*i})
						w.emplace_back(p);
			} else
				w.emplace_back(*i);
		}
		it = begin(w);
		end_it = end(w);
	} 

	if (randomize) {
		std::random_device rd;
		std::mt19937 g(rd());
		shuffle(it, end_it, g);
	}
	if (justone)
		end_it = it+1;

	execp_vector(verbose, start, end_start, it, end_it, exclude, only, 
	    maxargs);
	exit(1);
}
