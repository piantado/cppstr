#pragma once

// cppstr: python-like string conversion (str), parsing (str_to), and printing (print) for C++20.
//
// Round-trip guarantee: for the supported types (arithmetic types, std::string, and std::vector,
// deque, set, multiset, map, pair, tuple, array of those, nested arbitrarily),
//     str_to<T>(str(x)) == x
// holds, PROVIDED that no std::string inside x contains one of the structural characters
//     ,  :  [  ]  {  }  <  >
// (there is no quoting/escaping), and that empty strings do not appear as the *only* element of
// a vector/deque/set (since "[]" is read as an empty container). Floats round-trip exactly,
// except subnormals, which std::stod rejects (see str_to below).

#include <array>
#include <atomic>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <deque>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

//*******************************************************************************************
// Some quick iostream stuff to make life better
//*******************************************************************************************

// so sick of typing this horseshit
#define TAB <<"\t"<<
#define NL <<"\n"<<
#define ENDL <<std::endl;
#define ENDLL <<"\n"<<std::endl;
#define CERR std::cerr<<
#define COUT std::cout<<
#define COUTT std::cout<<"\t"<<
#define COUTTT std::cout<<"\t\t"<<

//*******************************************************************************************
// Exceptions
//*******************************************************************************************

// This is thrown by split<N> (and by str_to for fixed-size types) when a string has the wrong
// number of pieces, allowing us to recover from lines that are bad. what() describes the problem.
struct StringSplitException : public std::runtime_error {
	StringSplitException() : std::runtime_error("StringSplitException") {}
	explicit StringSplitException(const std::string& msg) : std::runtime_error(msg) {}
};

//*******************************************************************************************
// Some concepts we need
//*******************************************************************************************

/**
 * @concept HasStringMethod
 * @brief Checks if a (const) T has a member function `.string()`.
 */
template <typename T>
concept HasStringMethod = requires(const T& a) {
	{ a.string() };
};

/**
 * @concept HasOstreamOperator
 * @brief Checks if 'os << a' is valid for a standard ostream
 */
template <typename A>
concept HasOstreamOperator = requires(std::ostream& os, const A& a) {
	{ os << a } -> std::same_as<std::ostream&>;
};

//*******************************************************************************************
// Splitting strings
//*******************************************************************************************

/**
 * @brief Split returns a deque of s split up at the character delimiter.
 * The number of tokens is always (number of delimiters + 1), except for the empty string:
 * split("", ':')   -> []
 * split("a", ':')  -> ["a"]
 * split("a:", ':') -> ["a", ""]
 * split(":", ':')  -> ["", ""]
 * split(":a", ':') -> ["", "a"]
 * @param s
 * @param delimiter
 * @return
 */
inline std::deque<std::string> split(const std::string& s, const char delimiter) {
	std::deque<std::string> tokens;

	if(s.empty()) {
		return tokens;
	}

	size_t i = 0;
	while(i < s.size()) {
		size_t k = s.find(delimiter, i);

		if(k == std::string::npos) {
			tokens.push_back(s.substr(i));
			return tokens;
		}
		else {
			tokens.push_back(s.substr(i,k-i));
			i=k+1;
		}
	}

	// if we get here, that means that the last found k was the last
	// character, which means we need to append ""
	tokens.push_back("");
	return tokens;
}

/**
 * @brief Split with a fixed return size, useful in parsing csv.
 *        Throws StringSplitException if s does not have exactly N pieces.
 * @param s
 * @param delimiter
 * @return
 */
template<size_t N>
std::array<std::string, N> split(const std::string& s, const char delimiter) {
	auto q = split(s, delimiter);

	// must be the right size
	if(q.size() != N) {
		throw StringSplitException("String in split<N> has " + std::to_string(q.size()) +
		                           " not " + std::to_string(N) + " elements: " + s);
	}

	// convert to an array now that we know it's the right size
	std::array<std::string, N> out;
	size_t i = 0;
	for(auto& x : q) {
		out[i++] = std::move(x);
	}

	return out;
}

//*******************************************************************************************
// Internal helpers (not part of the public interface)
//*******************************************************************************************

namespace cppstr_detail {

	// is_specialization<std::vector<int>, std::vector>::value == true
	// (kept in this namespace so it can't collide with e.g. Fleet's global is_specialization)
	template<typename T, template<typename...> class Ref>
	struct is_specialization : std::false_type {};

	template<template<typename...> class Ref, typename... Args>
	struct is_specialization<Ref<Args...>, Ref> : std::true_type {};

	template<typename T>          struct is_std_array                    : std::false_type {};
	template<typename T, size_t N> struct is_std_array<std::array<T, N>> : std::true_type {};

	// Types whose str() representation is wrapped in brackets, and which may therefore contain
	// delimiters that must not be split on when they appear as elements of another container.
	template<typename T>
	inline constexpr bool is_bracketed_v =
		is_specialization<T, std::vector>::value   ||
		is_specialization<T, std::deque>::value    ||
		is_specialization<T, std::set>::value      ||
		is_specialization<T, std::multiset>::value ||
		is_specialization<T, std::map>::value      ||
		is_specialization<T, std::pair>::value     ||
		is_specialization<T, std::tuple>::value    ||
		is_std_array<T>::value;

	template<typename Tuple>               struct tuple_any_bracketed;
	template<typename... Ts>               struct tuple_any_bracketed<std::tuple<Ts...>>
		: std::bool_constant<(is_bracketed_v<Ts> || ...)> {};

	// the closing bracket for an opening bracket, or 0 if c is not an opening bracket
	constexpr char closer_for(char c) {
		switch(c) {
			case '[': return ']';
			case '{': return '}';
			case '<': return '>';
			default:  return 0;
		}
	}

	constexpr bool is_closer(char c) { return c == ']' || c == '}' || c == '>'; }

	/**
	 * @brief Position of the bracket that closes the opening bracket at s[open], or npos if
	 *        s[open] is not an opening bracket or the brackets are unbalanced/mismatched.
	 */
	inline size_t matching_close(std::string_view s, size_t open) {
		if(open >= s.size() || closer_for(s[open]) == 0) return std::string::npos;

		std::string stack; // stack of expected closing brackets
		for(size_t i = open; i < s.size(); i++) {
			const char c = s[i];
			if(const char cl = closer_for(c)) {
				stack.push_back(cl);
			}
			else if(is_closer(c)) {
				if(stack.empty() || stack.back() != c) return std::string::npos;
				stack.pop_back();
				if(stack.empty()) return i;
			}
		}
		return std::string::npos;
	}

	/**
	 * @brief If s is exactly open ... close, where the opening bracket matches the final one,
	 *        remove those two characters and return true. Otherwise leave s alone, return false.
	 */
	inline bool strip_enclosing(std::string& s, char open, char close) {
		if(s.size() >= 2 and s.front() == open and s.back() == close and
		   matching_close(s, 0) == s.size()-1) {
			s = s.substr(1, s.size()-2);
			return true;
		}
		return false;
	}

	/**
	 * @brief Legacy (lenient) bracket stripping used for containers of non-nested types:
	 *        remove a leading open and/or a trailing close, each independently.
	 */
	inline void strip_loose(std::string& s, char open, char close) {
		if(!s.empty() and s.front() == open) s.erase(0, 1);
		if(!s.empty() and s.back()  == close) s.pop_back();
	}

	template<bool Nested>
	void strip_brackets(std::string& s, char open, char close) {
		if constexpr (Nested) strip_enclosing(s, open, close);
		else                  strip_loose(s, open, close);
	}

	/**
	 * @brief Like split(s, delimiter), but ignores delimiters nested inside [] {} or <>.
	 *        Throws std::invalid_argument on unbalanced or mismatched brackets.
	 */
	inline std::deque<std::string> split_toplevel(const std::string& s, const char delimiter) {
		std::deque<std::string> tokens;
		if(s.empty()) return tokens;

		std::string stack; // stack of expected closing brackets
		size_t start = 0;
		for(size_t i = 0; i < s.size(); i++) {
			const char c = s[i];
			if(stack.empty() and c == delimiter) {
				tokens.push_back(s.substr(start, i-start));
				start = i+1;
			}
			else if(const char cl = closer_for(c)) {
				stack.push_back(cl);
			}
			else if(is_closer(c)) {
				if(stack.empty() || stack.back() != c)
					throw std::invalid_argument("cppstr: unbalanced or mismatched brackets in: " + s);
				stack.pop_back();
			}
		}
		if(!stack.empty())
			throw std::invalid_argument("cppstr: unclosed bracket in: " + s);

		tokens.push_back(s.substr(start)); // possibly "", matching split()'s semantics
		return tokens;
	}

	// split at top-level delimiters if any of the element types is nested; otherwise plain split.
	// (Plain split preserves the old behavior exactly for flat containers.)
	template<bool Nested>
	std::deque<std::string> split_for(const std::string& s, const char delimiter) {
		if constexpr (Nested) return split_toplevel(s, delimiter);
		else                  return ::split(s, delimiter);
	}

	/**
	 * @brief Shortest string that parses back to exactly x (locale-independent).
	 */
	template<typename F>
	std::string float_to_string(F x) {
#if defined(__cpp_lib_to_chars)
		char buf[128]; // far more than the longest shortest-repr of any standard floating type
		const auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), x);
		if(ec != std::errc{}) throw std::runtime_error("cppstr: std::to_chars failed");
		return std::string(buf, ptr);
#else
		// Fallback: not always shortest, but max_digits10 still guarantees an exact round trip.
		std::ostringstream ss;
		ss.imbue(std::locale::classic());
		ss << std::setprecision(std::numeric_limits<F>::max_digits10) << x;
		return ss.str();
#endif
	}

	// Range-checked narrowing for str_to on small integer types
	template<typename Narrow, typename Wide>
	Narrow narrow_checked(Wide v, const char* name) {
		if(!std::in_range<Narrow>(v)) throw std::out_of_range(std::string("str_to<") + name + ">");
		return static_cast<Narrow>(v);
	}

	inline void require_size(const std::deque<std::string>& parts, size_t n, const std::string& s, const char* what) {
		if(parts.size() != n) {
			throw StringSplitException(std::string("str_to<") + what + "> expected " + std::to_string(n) +
			                           " elements but got " + std::to_string(parts.size()) + ": " + s);
		}
	}
}

//*******************************************************************************************
// The str function and its overloads
//*******************************************************************************************

// Declare every overload before defining any of them, so that nested containers
// (e.g. vector<map<..>>) find the right overload regardless of definition order.
template<typename T>                                     std::string str(const T& x);
                                                         std::string str(const std::string& x);
template<typename... T>                                  std::string str(const std::tuple<T...>& x);
template<typename A, typename B>                         std::string str(const std::pair<A,B>& x);
template<typename T, size_t N>                           std::string str(const std::array<T,N>& a);
template<typename T, typename Al>                        std::string str(const std::vector<T,Al>& a, const std::string& _sep=",");
template<typename T, typename Al>                        std::string str(const std::deque<T,Al>& a);
template<typename K, typename V, typename C, typename Al> std::string str(const std::map<K,V,C,Al>& a);
template<typename T, typename C, typename Al>            std::string str(const std::set<T,C,Al>& a);
template<typename T, typename C, typename Al>            std::string str(const std::multiset<T,C,Al>& a);
template<typename T>                                     std::string str(const std::atomic<T>& a);
template<typename... Args>                               std::string str(const std::string& _sep, const Args&... args);

namespace cppstr_detail {
	// open + str(x0) + sep + str(x1) + ... + close
	template<typename Container>
	std::string join(const Container& c, std::string_view open, std::string_view sep, std::string_view close) {
		std::string out(open);
		bool first = true;
		for(const auto& x : c) {
			if(!first) out += sep;
			out += str(x);
			first = false;
		}
		out += close;
		return out;
	}
}

/**
 * @brief A pythonesque string function. This is overloaded for containers below.
 * @param x
 * @return
 */
template <typename T>
std::string str(const T& x) {
	using RawT   = std::remove_cvref_t<T>; // e.g. char[4] stays an array
	using CleanT = std::decay_t<T>;        // e.g. char[4] becomes char*

	// 0. nullptr itself
	if constexpr (std::is_same_v<CleanT, std::nullptr_t>) {
		return "nullptr";
	}
	// 1. Strings, string views, and C strings (return as std::string directly)
	else if constexpr (std::is_convertible_v<CleanT, std::string_view>) {
		if constexpr (std::is_pointer_v<RawT>) {
			if (x == nullptr) return "nullptr"; // std::string(nullptr) is undefined behavior
		}
		return std::string(std::string_view(x));
	}
	// 2. Character type ('a' -> "a")
	else if constexpr (std::is_same_v<CleanT, char>) {
		return std::string(1, x);
	}
	// 3. signed/unsigned char (int8_t/uint8_t) are numbers, not characters
	else if constexpr (std::is_same_v<CleanT, signed char> || std::is_same_v<CleanT, unsigned char>) {
		return std::to_string(static_cast<int>(x));
	}
	// 4. Floating point: shortest representation that round-trips exactly
	else if constexpr (std::is_floating_point_v<CleanT>) {
		return cppstr_detail::float_to_string(x);
	}
	// 5. Pointer types (format as hexadecimal address)
	else if constexpr (std::is_pointer_v<CleanT>) {
		if (x == nullptr) return "nullptr";
		std::ostringstream address;
		address << static_cast<const void*>(x);
		return address.str();
	}
	// 6. Objects with a .string() member function (whatever it returns gets str'd)
	else if constexpr (HasStringMethod<CleanT>) {
		return str(x.string());
	}
	// 7. Types printable via std::ostream (e.g., integers, bool, custom classes with operator<<)
	else if constexpr (HasOstreamOperator<CleanT>) {
		std::ostringstream ss;
		ss << x;
		return ss.str();
	}
	// 8. Primitive numeric fallback
	else if constexpr (requires { std::to_string(x); }) {
		return std::to_string(x);
	}
	else {
		static_assert(HasOstreamOperator<CleanT>, "No conversion to string available for type T!");
	}
}

inline std::string str(const std::string& x){
	// Non-template overload: preferred for std::string over the variadic separator overload below
	return x;
}

template<typename... T>
std::string str(const std::tuple<T...>& x){
	/**
	 * @brief Tuples print as <a,b,c>
	 */
	std::string out = "<";
	bool first = true;
	std::apply([&](const auto&... e) {
		((out += (first ? "" : ","), out += str(e), first = false), ...);
	}, x);
	return out + ">";
}

template<typename A, typename B>
std::string str(const std::pair<A,B>& x){
	/**
	 * @brief Pairs print as <a,b>
	 */
	return "<" + str(x.first) + "," + str(x.second) + ">";
}

template<typename T, size_t N>
std::string str(const std::array<T, N>& a){
	/**
	 * @brief Arrays print as <a,b,c>
	 */
	return cppstr_detail::join(a, "<", ",", ">");
}

template<typename T, typename Al>
std::string str(const std::vector<T,Al>& a, const std::string& _sep){
	/**
	 * @brief Vectors print as [a,b,c]  (with an optional separator in place of ",")
	 */
	return cppstr_detail::join(a, "[", _sep, "]");
}

template<typename T, typename Al>
std::string str(const std::deque<T,Al>& a){
	/**
	 * @brief Deques print as [a,b,c]
	 */
	return cppstr_detail::join(a, "[", ",", "]");
}

template<typename K, typename V, typename C, typename Al>
std::string str(const std::map<K,V,C,Al>& a){
	/**
	 * @brief Maps print as {k:v,k:v}
	 */
	std::string out = "{";
	bool first = true;
	for(const auto& [k, v] : a) {
		if(!first) out += ",";
		out += str(k) + ":" + str(v);
		first = false;
	}
	return out + "}";
}

template<typename T, typename C, typename Al>
std::string str(const std::set<T,C,Al>& a){
	/**
	 * @brief Sets print as {a,b,c}
	 */
	return cppstr_detail::join(a, "{", ",", "}");
}

template<typename T, typename C, typename Al>
std::string str(const std::multiset<T,C,Al>& a){
	/**
	 * @brief Multisets print as {a,a,b}
	 */
	return cppstr_detail::join(a, "{", ",", "}");
}

template<typename T>
std::string str(const std::atomic<T>& a){
	/**
	 * @brief Atomics print their (loaded) value
	 */
	return str(a.load());
}

template<typename... Args>
std::string str(const std::string& _sep, const Args&... args){
	/**
	 * @brief str(sep, a, b, c) gives str(a) + sep + str(b) + sep + str(c)
	 *        NOTE: the FIRST argument is always the separator.
	 */
	std::string out;
	bool first = true;
	((out += (first ? std::string() : _sep), out += str(args), first = false), ...);
	return out;
}


//*******************************************************************************************
// Conversions the other way
//*******************************************************************************************

/**
 * @brief Fleet includes this templated function to allow us to convert strings to a variety of formats.
 * 		  This is mostly used for reading data from text files. This recursively will unpack containers
 * 		  in the formats that str() writes:
 *            vector/deque  [a,b,c]        (brackets optional)
 *            set/multiset  {a,b,c}        (braces optional)
 *            map           {k:v,k:v}      (braces optional)
 *            pair          a:b  or  <a,b>
 *            tuple/array   <a,b,c>        (angle brackets optional)
 *        Nested containers are split only at top-level delimiters.
 *        Since it can handle Fleet types (e.g. defaultdata_t) it can be used to unpack data stored in
 *        strings (see e.g. Models/Sorting). Any other type T must be constructible as T{std::string},
 *        or you can add your own explicit specialization  template<> T str_to<T>(const std::string&).
 *
 *        Number parsing uses std::stoi/stod etc., and so is lenient: it skips leading whitespace, ignores
 *        trailing junk ("3.7" -> 3 for int), and depends on the C locale. std::stod also throws
 *        std::out_of_range on subnormal values (e.g. 1e-310).
 * @param s
 * @return
 */
template<typename T>
T str_to(const std::string& s);

// Explicit specializations for scalar types. These are inline because they are full
// specializations defined in a header (otherwise: multiple definitions at link time).
template<> inline std::string        str_to<std::string>       (const std::string& s) { return s; }
template<> inline int                str_to<int>               (const std::string& s) { return std::stoi(s); }
template<> inline long               str_to<long>              (const std::string& s) { return std::stol(s); }
template<> inline long long          str_to<long long>         (const std::string& s) { return std::stoll(s); }
template<> inline unsigned long      str_to<unsigned long>     (const std::string& s) { return std::stoul(s); }
template<> inline unsigned long long str_to<unsigned long long>(const std::string& s) { return std::stoull(s); }
template<> inline double             str_to<double>            (const std::string& s) { return std::stod(s); }
template<> inline float              str_to<float>             (const std::string& s) { return std::stof(s); }
template<> inline long double        str_to<long double>       (const std::string& s) { return std::stold(s); }
template<> inline bool               str_to<bool>              (const std::string& s) { return (s=="1" or s=="t" or s=="True" or s=="true"); } // 0/1 for t/f
// narrower integer types are parsed wide and then range-checked (std::out_of_range if they don't fit)
template<> inline unsigned int       str_to<unsigned int>      (const std::string& s) { return cppstr_detail::narrow_checked<unsigned int>  (std::stoul(s), "unsigned int"); }
template<> inline short              str_to<short>             (const std::string& s) { return cppstr_detail::narrow_checked<short>         (std::stoi(s),  "short"); }
template<> inline unsigned short     str_to<unsigned short>    (const std::string& s) { return cppstr_detail::narrow_checked<unsigned short>(std::stoul(s), "unsigned short"); }
template<> inline signed char        str_to<signed char>       (const std::string& s) { return cppstr_detail::narrow_checked<signed char>   (std::stoi(s),  "signed char"); }   // int8_t: a number, matching str()
template<> inline unsigned char      str_to<unsigned char>     (const std::string& s) { return cppstr_detail::narrow_checked<unsigned char> (std::stoul(s), "unsigned char"); } // uint8_t: a number, matching str()
template<> inline char               str_to<char>              (const std::string& s) {
	if(s.size() != 1) throw std::invalid_argument("str_to<char> needs exactly one character: \"" + s + "\"");
	return s[0];
}

template<typename T>
T str_to(const std::string& ss, char split_at) {
	// Here split_at is only for the TOP-LEVEL split -- everything below uses the str() delimiters
	using namespace cppstr_detail;

	static_assert(!std::is_same_v<T, std::string_view>,
	              "str_to<std::string_view> would return a dangling view; use std::string");

	std::string s = ss; // copy so its easy to modify

	if constexpr(cppstr_detail::is_specialization<T,std::map>::value) {
		using K = typename T::key_type;
		using V = typename T::mapped_type;
		constexpr bool nested = is_bracketed_v<K> || is_bracketed_v<V>;

		strip_brackets<nested>(s, '{', '}');

		T m;
		for(const auto& r : split_for<nested>(s, split_at)) {
			const auto kv = split_for<nested>(r, ':');
			require_size(kv, 2, r, "map entry");
			m.insert_or_assign(str_to<K>(kv[0]), str_to<V>(kv[1])); // later duplicates overwrite
		}
		return m;
	}
	else if constexpr(cppstr_detail::is_specialization<T,std::pair>::value) {
		using A = typename T::first_type;
		using B = typename T::second_type;
		constexpr bool nested = is_bracketed_v<A> || is_bracketed_v<B>;

		// a:b  (the original format) is tried first; then <a,b>  (what str() writes)
		auto parts = split_for<nested>(s, ':');
		if(parts.size() != 2 and strip_enclosing(s, '<', '>')) {
			parts = split_for<nested>(s, ',');
		}
		require_size(parts, 2, ss, "pair");
		return T{str_to<A>(parts[0]), str_to<B>(parts[1])};
	}
	else if constexpr(cppstr_detail::is_specialization<T,std::vector>::value or cppstr_detail::is_specialization<T,std::deque>::value) {
		using E = typename T::value_type;
		constexpr bool nested = is_bracketed_v<E>;

		strip_brackets<nested>(s, '[', ']'); // remove [...] if they are there (optional)

		T v;
		for(const auto& x : split_for<nested>(s, split_at)) {
			v.push_back(str_to<E>(x));
		}
		return v;
	}
	else if constexpr(cppstr_detail::is_specialization<T,std::set>::value or cppstr_detail::is_specialization<T,std::multiset>::value) {
		using E = typename T::key_type;
		constexpr bool nested = is_bracketed_v<E>;

		strip_brackets<nested>(s, '{', '}'); // remove {...} if they are there (optional)

		T ret;
		for(const auto& x : split_for<nested>(s, split_at)) {
			ret.insert(str_to<E>(x));
		}
		return ret;
	}
	else if constexpr(cppstr_detail::is_specialization<T,std::tuple>::value or cppstr_detail::is_std_array<T>::value) {
		constexpr size_t N = std::tuple_size_v<T>;
		constexpr bool nested = [] {
			if constexpr (cppstr_detail::is_std_array<T>::value) return is_bracketed_v<typename T::value_type>;
			else                                  return tuple_any_bracketed<T>::value;
		}();

		strip_enclosing(s, '<', '>'); // remove <...> if they are there (optional)

		auto parts = split_for<nested>(s, split_at);
		if(N == 1 and parts.empty()) parts.push_back(""); // "<>" is a single empty element
		require_size(parts, N, ss, cppstr_detail::is_std_array<T>::value ? "array" : "tuple");

		return [&]<size_t... I>(std::index_sequence<I...>) {
			// braced initialization guarantees left-to-right evaluation
			return T{ str_to<std::tuple_element_t<I, T>>(parts[I])... };
		}(std::make_index_sequence<N>{});
	}
	else {
		// Otherwise we must have a string constructor
		return T{s};
	}
}

// Default call with comma separator
template<typename T>
T str_to(const std::string& s) {
	return str_to<T>(s, ',');
}

//*******************************************************************************************
// IO functions
//*******************************************************************************************

// Global or inline lock for thread-safe console output
inline std::mutex output_lock;
//inline constexpr const char* OUTPUT_SEP = " ";
inline constexpr std::string OUTPUT_SEP = "\t";  // standard output separator (inline: one object across all TUs)


/**
 * @brief Print one value using str() (assumes output_lock is already held, if needed)
 */
template <typename A>
void print_single_element(std::ostream& o, const A& val) {
	o << str(val);
}

/**
 * @brief The line that print(args...) writes: str(args) separated by OUTPUT_SEP, then "\n"
 */
template <typename... ARGS>
std::string print_line(const ARGS&... args) {
	std::string line;
	bool first = true;
	((line += (first ? std::string() : std::string(OUTPUT_SEP)), line += str(args), first = false), ...);
	line += '\n'; // '\n' rather than std::endl, to avoid flushing on every line
	return line;
}

/**
 * @brief Prints variadic arguments separated by OUTPUT_SEP (assumes output_lock is already held)
 */
template <typename... ARGS>
void output_with_lock(std::ostream& o, const ARGS&... args) {
	o << print_line(args...);
}

/**
 * @brief Lock output_lock and print to std::cout. The conversion to string happens before
 *        locking, and the line is written in one call, so lines from different threads never interleave.
 */
template <typename... ARGS>
void print(const ARGS&... args) {
	const std::string line = print_line(args...);
	std::lock_guard<std::mutex> guard(output_lock);
	std::cout << line;
}

/**
 * @brief Lock output_lock and print to std::cerr
 */
template <typename... ARGS>
void eprint(const ARGS&... args) {
	const std::string line = print_line(args...);
	std::lock_guard<std::mutex> guard(output_lock);
	std::cerr << line;
}
