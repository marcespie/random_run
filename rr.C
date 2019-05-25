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

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <regex>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using std::filesystem::path;
using directory_it = std::filesystem::recursive_directory_iterator;
using std::vector;
using std::regex;
using std::regex_error;
using std::cerr;
using std::cout;
using std::ifstream;
using std::string;
using std::ostream_iterator;


struct options;

[[noreturn]] void usage();
[[noreturn]] void system_error(const char*);
auto find_end(const char*);
void add_regex(vector<regex>&, const char*, const options&);
const options get_options(int, char*[]);
auto path_vector(char*[], int);
void add_lines(vector<path>&, const char*);
[[noreturn]] void exec(const vector<const char*>&);
void deal_with_child(int, bool);
bool any_match(const char*, const vector<regex>&);
void may_add(vector<const char*>&, const char*, const options&);
template<typename T> void get_integer_value(const char*, T&);
template<typename it> [[noreturn]] auto run_commands(it, it, it, it, 
    const options&);

//
// boilerplate support
//
void
usage()
{
	cerr << "Usage: rr [-1EeiNOrv] [-l file] [-n maxargs] [-o regex] [-x regex] cmd [flags --] params...\n";
	exit(1);
}

void
system_error(const char* msg)
{
	auto e = strerror(errno);
	cerr << msg << ": " << e << "\n";
	exit(1);
}

#if !defined(__OpenBSD__)
int
pledge(const char*, const char*)
{
	return 0;
}
#endif

//
// option handling code
//
struct options {
	bool justone = false;
	bool verbose = false;
	bool recursive = false;
	bool randomize = true;
	bool once = false;
	bool exitonerror = false;
	bool nocase = false;
	bool eregex = false;
	size_t maxargs = 0;
	vector<regex> exclude, only;
	vector<char*> list;
};

auto
find_end(const char* s)
{
	return s + strlen(s);
}

template<typename T>
void
get_integer_value(const char* s, T& r)
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

void
add_regex(vector<regex>& v, const char* arg, const options& o)
{
	try {
		using namespace std::regex_constants;
		auto flags = o.eregex ? extended : basic;
		if (o.nocase)
			flags |= icase;
		v.emplace_back(arg, flags);
	} catch (regex_error& e) {
		cerr << "bad regex " << arg << ": " << e.what() << "\n";
		usage();
	}
}

const options 
get_options(int argc, char* argv[])
{
	options o;

	for (int ch; (ch = getopt(argc, argv, "v1eEil:rn:No:Ox:")) != -1;)
		switch(ch) {
		case 'v':
			o.verbose = true;
			break;
		case 'n':
			get_integer_value(optarg, o.maxargs);
			break;
		case 'N':
			o.randomize = false;
			break;
		case 'r':
			o.recursive = true;
			break;
		case '1':
			o.justone = true;
			break;
		case 'i':
			o.nocase = true;
			break;
		case 'e':
			o.exitonerror = true;
			break;
		case 'E':
			o.eregex = true;
			break;
		case 'o':
			add_regex(o.only, optarg, o);
			break;
		case 'O':
			o.once = true;
			break;
		case 'l':
			o.list.push_back(optarg);
			break;
		case 'x':
			add_regex(o.exclude, optarg, o);
			break;
		default:
			usage();
		}
	return o;
}

// 
// support for massaging parameters
//
auto
path_vector(char* av[], int ac)
{
	vector<path> result;
	for (int i = 0; i != ac; i++)
		result.emplace_back(av[i]);
	return result;
}


void
add_lines(vector<path>& r, const char* fname)
{
	ifstream f;
	f.open(fname);
	if (!f.is_open()) {
		cerr << "failed to open " << fname << "\n";
		usage();
	}
	for (string line; getline(f, line); ) 
		r.emplace_back(line);
}

// filtering on a list of regex
bool 
any_match(const char* s, const vector<regex>& x)
{
	for (auto& r: x)
		if (regex_match(s, r))
			return true;
	return false;
}

// actually running commands
void
exec(const vector<const char*>& v)
{
	// XXX paths are essentially "movable" strings, so they're const
	// we can type-pune the const because we won't ever return
	execvp(v[0], const_cast<char**>(v.data()));
	system_error("execvp");
}

// ... and maybe coming back for more
void
deal_with_child(int pid, bool exitonerror)
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
			if (exitonerror)
				exit(s);
		}
	} else {
		auto s = WTERMSIG(r);
		cerr << "Command exited on signal #"<< s << "\n";
		if (exitonerror) {
			kill(getpid(), s);
			// in case we didn't die
			exit(1);
		}
	}
}

void
may_add(vector<const char*>& v, const char* s, const options& o)
{
	// notice the asymetry: we "exclude" anything
	if (any_match(s, o.exclude))
		return;
	// BUT "only" doesn't kick in if it's not been mentioned
	if (o.only.size() == 0 || any_match(s, o.only))
		v.push_back(s);
}

// the core of the runner
template<typename it>
auto
run_commands(it a1, it b1, it a2, it b2, const options& o)
{
	vector<const char*> v;
	// first push the actual command (constant across all runs */
	for (auto i = a1; i != b1; ++i)
		v.push_back(i->c_str());

	auto reset = v.size();
	if (o.maxargs && v.size() >= o.maxargs) {
		cerr << "Can't obey -n" << o.maxargs << 
		    ", initial command is too long ("
		    << v.size() << " words)\n";
		usage();
	}

	auto i = a2;

	while (true) {
		// then the filtered params (some ?)
		for (;i != b2 && v.size() != o.maxargs; ++i)
			may_add(v, i->c_str(), o);
		if (o.verbose) {
			copy(begin(v), end(v), 
			    ostream_iterator<const char*>(cout, " "));
			cout << std::endl;
		}
		v.push_back(nullptr);

		if (i != b2 && !o.once) {
			// we didn't do them all yet, so get ready for
			// another round
			auto k = fork();
			if (k == -1)
				system_error("fork");
			else if (k == 0)
				exec(v);
			else
				deal_with_child(k, o.exitonerror);
			v.resize(reset);
		} else 
			// XXX sneaky end of loop, exec doesn't return
			exec(v);
	}
}


int 
main(int argc, char* argv[])
{
	if (pledge("stdio rpath proc exec", NULL) != 0)
		system_error("pledge");

	auto o = get_options(argc, argv);

	argc -= optind;
	argv += optind;
	if (argc == 0)
		usage();

	// create the actual list of args to process
	auto v = path_vector(argv, argc);
	for (auto& filename: o.list)
		add_lines(v, filename);

	auto start = begin(v);

	// first parameter is always the actual program name
	// this computes [start, end_start[ (immovable program)
	// and [it, end_it[ (actual parameters)
	auto start_parm = start+1;
	auto end_start = start_parm;

	// and then we skip anything upto a -- if we see one
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
		++it; // don't forget to skip the -- !
	}

	auto end_it = end(v);

	// in the recursive case, fill w with actual file names
	// and have [it, end_it[  point into w.
	vector<path> w; // ... so w must be at function scope to avoid gc
	if (o.recursive) {
		for (auto i = it; i != end_it; ++i) {
			if (is_directory(*i)) {
				// we do also exclude directories
				if (!any_match(i->c_str(), o.exclude))
					for (auto& p: directory_it{*i})
						if (!is_directory(p))
							w.emplace_back(p);
			} else
				w.emplace_back(*i);
		}
		it = begin(w);
		end_it = end(w);
	} 

	if (pledge("stdio proc exec", NULL) != 0)
		system_error("pledge");

	// the actual algorithm that started it all
	if (o.randomize) {
		std::random_device rd;
		std::mt19937 g(rd());
		shuffle(it, end_it, g);
	}
	if (o.justone)
		end_it = it+1;

	run_commands(start, end_start, it, end_it, o);
}
