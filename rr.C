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
#include <limits>
#include <random>
#include <regex>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include <set>

using std::filesystem::path;
using std::filesystem::is_directory;
using directory_it = std::filesystem::recursive_directory_iterator;
using std::vector;
using std::regex;
using std::regex_error;
using std::cerr;
using std::cout;
using std::ifstream;
using std::string;
using std::ostream_iterator;
using std::numeric_limits;
using std::set;

#if !defined(MYNAME)
const auto MYNAME = "rr";
#endif

struct options;

[[noreturn]] void usage();
[[noreturn]] void system_error(const char*);
auto find_end(const char*);
void add_regex(vector<regex>&, const char*, const options&);
const options get_options(int, char*[], char*[]);
auto path_vector(char*[], int);
void add_lines(vector<path>&, const char*);
[[noreturn]] void exec(const vector<const char*>&);
void deal_with_child(int, bool);
bool any_match(const char*, const vector<regex>&);
bool keep(const char*, const options&);
template<typename T> void get_integer_value(const char*, T&);
template<typename it> [[noreturn]] auto run_commands(it, it, it, it, 
    const options&);
size_t compute_maxsize(char*[], size_t);

//
// boilerplate support
//
void
usage()
{
	cerr << "Usage: " << MYNAME << " [-1dDEeiNOpRrv] [-l file] [-m margin] [-n maxargs] [-o regex] [-s start]\n\t[-x regex] cmd [flags --] params...\n";
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

const auto MAXSIZE = numeric_limits<size_t>::max();
//
// option handling code
//
struct options {
	bool justone = false;
	bool verbose = false;
	bool recursive = false;
	bool recursedirs = false;
	bool randomize = true;
	bool once = false;
	bool exitonerror = false;
	bool nocase = false;
	bool eregex = false;
	bool printonly = false;
	bool rotate = false;
	bool dashdash = true;
	size_t maxargs = MAXSIZE;
	size_t margin = 0;
	size_t maxsize;
	vector<regex> start, exclude, only;
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
		cerr << "Bad numeric value " << s << ": " << 
		    make_error_code(e).message() << "\n";
		usage();
	}
	if (ptr != p) {
		cerr << "Trailing chars after numeric parameter: " << s << "\n";
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
		cerr << "Bad regex " << arg << ": " << e.what() << "\n";
		usage();
	}
}

size_t
compute_maxsize(char* envp[], size_t margin)
{
	// maxargs for the shell, - path lookup for argv[0]
	auto maxsize = static_cast<size_t>(
	    sysconf(_SC_ARG_MAX) - pathconf("/", _PC_PATH_MAX));

	// need to take envp into account
	for (int i = 0; envp[i] != NULL; ++i)
		maxsize -= strlen(envp[i])+1;
	return maxsize - margin;
}

const options 
get_options(int argc, char* argv[], char* envp[])
{
	options o;

	for (int ch; (ch = getopt(argc, argv, "v1eDdEil:rRn:m:No:Ox:ps:")) != -1;)
		switch(ch) {
		case 'd':
			o.dashdash = false;
			break;
		case 'D':
			o.recursedirs = true;
			o.recursive = true;
			break;
		case 'v':
			o.verbose = true;
			break;
		case 'p':
			o.printonly = true;
			o.verbose = true;
			break;
		case 'n':
			get_integer_value(optarg, o.maxargs);
			break;
		case 'm':
			get_integer_value(optarg, o.margin);
			break;
		case 'N':
			o.randomize = false;
			break;
		case 'r':
			o.recursive = true;
			break;
		case 'R':
			o.rotate = true;
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
		case 's':
			add_regex(o.start, optarg, o);
			break;
		default:
			usage();
		}
	if (o.printonly)
		o.maxsize = MAXSIZE;
	else
		o.maxsize = compute_maxsize(envp, o.margin);

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
	if (is_directory(fname)) {
		cerr << "Can't read directory: " << fname << "\n";
		exit(1);
	}
	f.open(fname);
	if (!f.good()) {
		auto e = strerror(errno);
		cerr << "Failed to open " << fname << ": " << e << "\n";
		exit(1);
	}
	for (string line; getline(f, line); )
		r.emplace_back(line);
	if (f.bad()) {
		auto e = strerror(errno);
		cerr << "Error while reading " << fname << ": " << e << "\n";
		exit(1);
	}
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
	// let's type-pune the const, we won't ever return anyway
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

bool
keep(const char* s, const options& o)
{
	// notice the asymetry: we "exclude" anything
	if (any_match(s, o.exclude))
		return false;
	// BUT "only" doesn't kick in if it's not been mentioned
	return o.only.size() == 0 || any_match(s, o.only);
}

// the core of the runner
template<typename it>
auto
run_commands(it a1, it b1, // the actual command that doesn't change
    it a2, it b2, // parameters to batch through execs
    const options& o)
{
	vector<const char*> v;
	// first push the actual command (constant across all runs)
	for (auto i = a1; i != b1; ++i)
		v.push_back(i->c_str());

	size_t initial = 0;
	for (auto& x: v)
		initial += strlen(x)+1;

	auto reset = v.size();
	if (v.size() >= o.maxargs) {
		cerr << "Can't obey -n" << o.maxargs << 
		    ", initial command is too long ("
		    << v.size() << " words)\n";
		usage();
	}

	auto i = a2;

	for(;;v.resize(reset)) {
		// then the filtered params (some ?)
		size_t current = initial;
		for (;i != b2 && v.size() != o.maxargs; ++i) {
			auto s = i->c_str();
			if (!keep(s, o))
				continue;
			if (current + strlen(s)+1 >= o.maxsize)
				break;
			current += strlen(s)+1;
			v.push_back(s);
		}
		if (o.verbose) {
			copy(begin(v), end(v), 
			    ostream_iterator<const char*>(cout, " "));
			cout << std::endl;
		}
		v.push_back(nullptr);

		if (i != b2 && !o.once) {
			if (o.printonly)
				continue;
			// we didn't do them all yet, so get ready for
			// another round
			auto k = fork();
			if (k == -1)
				system_error("fork");
			else if (k == 0)
				exec(v);
			else
				deal_with_child(k, o.exitonerror);
		} else {
			if (o.printonly)
				break;
			// XXX sneaky end of loop, exec doesn't return
			exec(v);
		}
	}
	exit(0);
}


template<class T>
void
recurse(const T& it, vector<path>& w, bool recursedirs)
{
	if (recursedirs) {
		// that one is a bit tricky: we first need to record every
		// directory that doesn't have subdirectories
		set<path> seen;
		for (auto& p: directory_it{*it}) {
			if (is_directory(p)) {
				seen.emplace(p);
				seen.erase(p.path().parent_path());
			}
		}
		// ... then we can build our actual list
		for (auto& p: seen)
			w.emplace_back(p);
	} else {
		for (auto& p: directory_it{*it})
			if (!is_directory(p))
				w.emplace_back(p);
	}

}

int 
main(int argc, char* argv[], char* envp[])
{
	if (pledge("stdio rpath proc exec", NULL) != 0)
		system_error("pledge");

	auto o = get_options(argc, argv, envp);

	argc -= optind;
	argv += optind;
	// create the actual list of args to process
	auto v = path_vector(argv, argc);
	for (auto& filename: o.list)
		add_lines(v, filename);

	// set things up for o.printonly: no cmd, only args
	auto cmd = begin(v);
	auto end_cmd = begin(v);
	auto args = cmd;
	auto end_args = end(v);


	if (!o.printonly) {
		if (v.size() == 0) {
			cerr << "Error: " << MYNAME << " requires a cmd\n";
			usage();
		}
		// first parameter is always the actual program name
		// this computes [cmd, end_cmd[ (immovable program)
		// and [args, end_args[ (actual parameters)
		auto mark = cmd+1; // this is the "new start"

		// and then we skip anything upto a -- if we see one
		for (args = mark; args != end(v); ++args)
			if (strcmp(args->c_str(), "--") == 0)
				break;
		if (args == end(v)) {
			end_cmd = mark;
			args = mark;
		} else {
			end_cmd = args;
			++args;	// don't keep -- in the list of parameters
			if (o.dashdash)	// choose whether we keep it as an option
				end_cmd++;
		}
	}


	// in the recursive case, fill w with actual file names
	// and have [args, end_args[  point into w.
	vector<path> w; // ... so w must be at function scope to avoid gc
	if (o.recursive) {
		for (auto it = args; it != end_args; ++it) {
			if (is_directory(*it)) {
				// we do also exclude directories
				if (!any_match(it->c_str(), o.exclude))
					recurse(it, w, o.recursedirs);
			} else
				w.emplace_back(*it);
		}
		args = begin(w);
		end_args = end(w);
	}

	if (o.start.size())
		for (auto scan = args; scan != end_args; ++scan)
			if (any_match(scan->c_str(), o.start))
				args = scan;
	if (pledge(o.printonly ? "stdio" : "stdio proc exec", NULL) != 0)
		system_error("pledge");

	if (o.justone && end_args == args) {
		cerr << "Error: " << MYNAME << "-1 requires arguments\n";
		usage();
	}
	using disttype = std::uniform_int_distribution<decltype(end_args-args)>;
	// the actual algorithm that started it all
	if (o.randomize) {
		std::random_device rd;
		std::mt19937 g(rd());
		if (o.justone || o.rotate) {
			disttype dis(0, end_args-args-1);
			if (o.rotate)
				rotate(args, args + dis(g), end_args);
		    	else
				swap(args[0], args[dis(g)]);
		} else 
			shuffle(args, end_args, g);
	}
	if (o.justone)
		end_args = args+1;

	run_commands(cmd, end_cmd, args, end_args, o);
}
