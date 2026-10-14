#pragma once 

#include <string>

#include <sstream>
#include <queue>
#include <array>
#include <map>
#include <set>
#include <atomic>
#include <string>

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
// Some helpful functions and classes
//*******************************************************************************************

// This is thrown by split when it has the wrong number of things
// allowing us to recover from lines that are bad
struct StringSplitException : public std::exception { };

//*******************************************************************************************
// Some concepts we need
//*******************************************************************************************

#include <concepts>
#include <type_traits>

/**
 * @concept HasString
 * @brief Checks if a type T has a member function or member variable named `.string()`.
 */
template <typename T>
concept HasStringMethod = requires(T a) {
    { a.string() };
};

// 1. Concept: Checks if 'os << a' is valid for any standard ostream
template <typename A>
concept HasOstreamOperator = requires(std::ostream& os, const A& a) {
    { os << a } -> std::same_as<std::ostream&>;
};

// 2. Concept: Checks if str<A>(a) exists AND can be printed to ostream
template <typename A>
concept HasStrSpecialization = requires(std::ostream& os, const A& a) {
    { os << str<A>(a) };
};

//*******************************************************************************************
// Splitting strings
//*******************************************************************************************

/**
 * @brief Split is returns a deque of s split up at the character delimiter. 
 * It handles these special cases:
 * split("a:", ':') -> ["a", ""]
 * split(":", ':')  -> [""] // TODO: Shouldn't this be ["",""]?
 * split(":a", ':') -> ["", "a"]
 * @param s
 * @param delimiter
 * @return 
 */
std::deque<std::string> split(const std::string& s, const char delimiter) {
	std::deque<std::string> tokens;
	
	if(s.length() == 0) {
		return tokens;
	}
	
	size_t i = 0;
	while(i < s.size()) {
		size_t k = s.find(delimiter, i);
		
		if(k == std::string::npos) {
			tokens.push_back(s.substr(i,std::string::npos));
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
 * @brief Split with a fixed return size, useful in parsing csv
 * @param s
 * @param delimiter
 * @return 
 */
template<size_t N>
std::array<std::string, N> split(const std::string& s, const char delimiter) {
	std::array<std::string, N> out;
	
	auto q = split(s, delimiter);
	
	// must be the right size 
	if(q.size() != N) {
		std::cerr << "*** String in split<N> has " << q.size() << " not " << N << " elements: " << s << std::endl;
		throw StringSplitException();
	}
	
	// convert to an array now that we know it's the right size
	size_t i = 0;
	for(auto& x : q) {
		out[i++] = x;
	}

	return out;
}

//*******************************************************************************************
// The str function and its specializations
//*******************************************************************************************

template<typename T> std::string str(const T& x);

/**c
 * @brief A pythonesque string function. This gets specialized in many ways. 
 * @param x
 * @return 
 */	 
template <typename T>
std::string str(const T& x) {
    // Decay type to strip const/volatile and references for clean comparison
    using CleanT = std::decay_t<T>;

    // 1. Strings and String Views (return as std::string directly)
    if constexpr (std::is_convertible_v<CleanT, std::string_view>) {
        return std::string(x);
    }
    // 2. Character types ('a' -> "a")
    else if constexpr (std::is_same_v<CleanT, char>) {
        return std::string(1, x);
    }
    // 3. Pointer types (format as hexadecimal address)
    else if constexpr (std::is_pointer_v<CleanT>) {
        if (x == nullptr) return "nullptr";
        std::ostringstream address;
        address << static_cast<const void*>(x);
        return address.str();
    }
    // 4. Objects with a .string() member function
    else if constexpr (HasStringMethod<CleanT>) {
        return x.string();
    }
    // 5. Types printable via std::ostream (e.g., custom classes with operator<<, floating-point)
    else if constexpr (HasOstreamOperator<CleanT>) {
        std::ostringstream ss;
        ss << x;
        return ss.str();
    }
    // 6. Primitive numeric fallback
    else if constexpr (requires { std::to_string(x); }) {
        return std::to_string(x);
    }
    else {
        static_assert(HasOstreamOperator<CleanT>, "No conversion to string available for type T!");
    }
}

std::string str(const std::string& x){
	// Need to specialize this, otherwise std::to_string fails
	return x;
}

template<typename... T, size_t... I >
std::string str(const std::tuple<T...>& x, std::index_sequence<I...> idx){
	/**
	 * @brief A pythonesque string function
	 * @param x
	 * @return 
	 */
	return "<" + ( (str(std::get<I>(x)) + ",") + ...) + ">";
}

template<typename... T>
std::string str(const std::tuple<T...>& x ){
	/**
	 * @brief A pythonesque string function
	 * @param x
	 * @return 
	 */
	return str(x, std::make_index_sequence<sizeof...(T)>() );
}

template<typename A, typename B >
std::string str(const std::pair<A,B> x){
	/**
	 * @brief A pythonesque string function
	 * @param x
	 * @return 
	 */
	return "<" + str(x.first) + "," + str(x.second) + ">";
}

template<typename T, size_t N>
std::string str(const std::array<T, N>& a ){
	/**
	 * @brief A pythonesque string function
	 * @param x
	 * @return 
	 */
	std::string out = "<";
	for(auto& x : a) {
		out += str(x) + ",";
	}
	
	if(a.size()>0) 
		out.erase(out.size()-1); // remove that last dumb comma
	
	return out+">";
}


template<typename T>
std::string str(const std::vector<T>& a, const std::string _sep=","){
	/**
	 * @brief A pythonesque string function
	 * @param x
	 * @return 
	 */
	std::string out = "[";
	for(const auto& x : a) {
		out += str(x) + _sep;
	}
	if(a.size()>0) 
		out.erase(out.size()-1); // remove that last dumb comma
	
	return out+"]";
}

template<typename T, typename U>
std::string str(const std::map<T,U>& a ){
	/**
	 * @brief A pythonesque string function
	 * @param x
	 * @return 
	 */
	std::string out = "{";
	for(auto& x : a) {
		out += str(x.first) + ":" + str(x.second) + ",";
	}
	if(a.size()>0) 
		out.erase(out.size()-1); // remove that last dumb comma
	out += "}";
	
	return out;
}

template<typename T>
std::string str(const std::set<T>& a ){
	/**
	 * @brief A pythonesque string function
	 * @param x
	 * @return 
	 */
	std::string out = "{";
	for(auto& x : a) {
		out += str(x)+",";
	}
	if(a.size()>0) 
		out.erase(out.size()-1); // remove that last dumb comma
	out += "}";
	
	return out;
}


template<typename T>
std::string str(const std::atomic<T>& a ){
	/**
	 * @brief A pythonesque string function
	 * @param x
	 * @return 
	 */
	return str(a.load());
}


template<typename... Args>
std::string str(std::string _sep, Args... args){

	std::string out = "";
	((out += str(args)+_sep), ...);
	
	// at the end of this we have one extra sep, so let's delete it 
	out.erase(out.size()-_sep.size());
	
	return out; 
}


//*******************************************************************************************
// Conversions the other way
//*******************************************************************************************

/**
 * @brief Fleet includes this templated function to allow us to convert strings to a variety of formats. 
 * 		  This is mostly used for reading data from text files. This recursively will unpack containers
 * 		  with some standard delimiters. Since it can handle Fleet types (e.g. defaultdata_t) it
 *        can be used to unpack data stored in strings (see e.g. Models/Sorting)
 * @param s
 * @return 
 */
template<typename T>
T str_to(const std::string& s);

template<typename T>
T str_to(const std::string& ss, char split_at) {
	// Here split_at is only for the TOP-LEVEL split --everythign below is different
	auto s = ss; // copy so its easy to modify (NOT ideal but for now...)
	
	// process some special cases here
	if constexpr(is_specialization<T,std::map>::value) {
		T m;
		for(auto& r : split(s, split_at)) { // TODO might want a diff delim here
			auto [x, y] = split<2>(r, ':');
			m[str_to<typename T::key_type>(x)] = str_to<typename T::mapped_type>(y);	
		}
		return m;
	}
	if constexpr(is_specialization<T,std::pair>::value) {
		auto [x,y] = split<2>(s, ':');
		return {str_to<typename T::first_type>(x), str_to<typename T::second_type>(y)};		
	}
	else if constexpr(is_specialization<T,std::vector>::value) {
		
		// remove [...] if they are there (optional)
		if(s.front() == '[') s.erase(0,1);
		if(s.back()  == ']') s.pop_back();
		
		T v;
		for(auto& x : split(s, split_at)) {
			v.push_back(str_to<typename T::value_type>(x));
		}
		return v;
	}
	else if constexpr(is_specialization<T,std::multiset>::value) {
		if(s.front() == '{') s.erase(0,1);
		if(s.back()  == '}') s.pop_back();
		
		T ret;
		for(auto& x : split(s, split_at)) {
			ret.insert( str_to<typename T::key_type>(x) );
		}
		return ret; 
	}
	else if constexpr(is_specialization<T,std::set>::value) {
		if(s.front() == '{') s.erase(0,1);
		if(s.back()  == '}') s.pop_back();
		
		T ret;
		for(auto& x : split(s, split_at)) {
			ret.insert( str_to<typename T::key_type>(x));
		}
		return ret; 
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

template<> std::string   str_to(const std::string& s) { return s; }
template<> int           str_to(const std::string& s) { return std::stoi(s); }
template<> long          str_to(const std::string& s) { return std::stol(s); }
template<> unsigned long str_to(const std::string& s) { return std::stoul(s); }
template<> double        str_to(const std::string& s) { return std::stod(s); }
template<> float         str_to(const std::string& s) { return std::stof(s); }
template<> bool          str_to(const std::string& s) { return (s=="1" or s=="t" or s=="True" or s=="true"); } // 0/1 for t/f

//template<typename T, typename U>
//std::pair<T,U> str_to(const std::string s) {	
//	auto [x,y] = split<2>(s, ':');
//	return {str_to<T>(x), str_to<U>(y)};		
//}

//*******************************************************************************************
// IO functions
//*******************************************************************************************

#include <iostream>
#include <mutex>

// Global or inline lock for thread-safe console output
inline std::mutex output_lock;
//inline constexpr const char* OUTPUT_SEP = " ";
constexpr std::string OUTPUT_SEP = "\t";  // standard output separator


/**
 * @brief Thread-safe print helper (assumes output_lock is already held)
 */
template <typename A>
void print_single_element(std::ostream& o, const A& val) {
    if constexpr (HasOstreamOperator<A>) {
        o << val;
    } 
    else if constexpr (HasStrSpecialization<A>) {
        o << str<A>(val);
    } 
    else {
        static_assert(HasOstreamOperator<A> || HasStrSpecialization<A>, 
                      "Type does not support direct ostream printing or str<A>()!");
    }
}

/**
 * @brief Prints variadic arguments separated by OUTPUT_SEP using C++17 Fold Expressions
 */
template <typename FIRST, typename... ARGS>
void output_with_lock(std::ostream& o, const FIRST& first, const ARGS&... args) {
    print_single_element(o, first);
    
    // Fold expression: replaces manual recursive template instantiation
    if constexpr (sizeof...(ARGS) > 0) {
        ((o << OUTPUT_SEP, print_single_element(o, args)), ...);
    }
    
    o << '\n'; // Prefer '\n' over std::endl to prevent unnecessary buffer flushing
}

/**
 * @brief Lock output_lock and print to std::cout
 */
template <typename... ARGS>
void print(const ARGS&... args) {
    std::lock_guard<std::mutex> guard(output_lock);
    output_with_lock(std::cout, args...);
}

/**
 * @brief Lock output_lock and print to std::cerr
 */
template <typename... ARGS>
void eprint(const ARGS&... args) {
    std::lock_guard<std::mutex> guard(output_lock);
    output_with_lock(std::cerr, args...);
}