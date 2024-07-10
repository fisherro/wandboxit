/*
Send code to be compiled and run by wandbox.
*/

// g++-mp-13 -std=c++17 wandbox.cpp -o wandbox -lcurl

/*
// Example request
{
    "code": "...",
    "options": "warning,gnu++1y",
    "compiler": "gcc-head",
    "compiler-option-raw": "-Dx=hogefuga\n-O3"
}
// Example submission
$ curl -H "Content-type: application/json" -d @test.json https://wandbox.org/api/compile.json
// Example response
{
    "status": 0,
    "compiler_message": "...",
    "program_message": "...",
    "compiler_error": "...",
    "program_output": "..."
}
*/

#include "nlohmann/json.hpp"
#include <curl/curl.h>
#include <iostream>
#include <string>
#include <iterator>
#include <fstream>
#include <vector>

namespace {
	size_t curl_callback(
		void* contents, size_t size, size_t nmemb, void* user_data)
	{
		if (not user_data) {
			std::cerr << "ERROR: curl_callback user_data is NULL\n";
			return 0;
		}
		std::string* data = reinterpret_cast<std::string*>(user_data);
		size_t total = size * nmemb;
		data->append(reinterpret_cast<char*>(contents), total);
		return total;
	}

	std::string post(std::string_view request)
	{
		std::string response;
		curl_global_init(CURL_GLOBAL_DEFAULT);
		CURL *curl = curl_easy_init();
		if (not curl) {
			std::cerr << "ERROR curl_easy_init\n";
		} else {
			curl_easy_setopt(curl, CURLOPT_URL,
					"https://wandbox.org/api/compile.json");
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_callback);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
			curl_easy_setopt(curl, CURLOPT_CA_CACHE_TIMEOUT, 604800L);
			curl_easy_setopt(curl, CURLOPT_USERAGENT, "wb/1.0");
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.data());
			curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, request.size());
			curl_slist* headers = nullptr;
			headers = curl_slist_append(headers, "Content-type: application/json");
			//TODO: Error checking?
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
			CURLcode res = curl_easy_perform(curl);
			curl_slist_free_all(headers);
			if (CURLE_OK != res) {
				std::cerr << "ERROR curl_easy_perform: "
					<< curl_easy_strerror(res) << '\n';
			}
			curl_easy_cleanup(curl);
		}
		curl_global_cleanup();
		return response;
	}
}

int main(const int argc, const char** argv)
{
	// The fstream ctors still don't take string_views!?
	// So we'll parse the args into strings instead.
	std::vector<std::string> args(argv + 1, argv + argc);
	if (args.empty()) {
		std::cerr << "Give me the name of a file to compile!\n";
		return EXIT_FAILURE;
	}
	std::ifstream file(args[0]);
	std::string code(std::istreambuf_iterator<char>{file}, {});
	nlohmann::json jrequest = {
		{ "options", "warning,gnu++17" },
		{ "compiler", "gcc-head"},
	};
	jrequest["code"] = code;
	std::string request{jrequest.dump()};
#if 0 // for debugging
	std::cout << "=====\n";
	std::cout << request << '\n';
	std::cout << "=====\n";
#endif
	auto response = post(request);
	auto jresponse{nlohmann::json::parse(response)};
#if 0 // For debugging
	std::cout << "*****\n";
	std::cout << jresponse.dump(4) << '\n';
	std::cout << "*****\n";
#endif
	std::cout << "\x1b[32m"; // green
	std::cout << jresponse[0]["compiler_message"].template get<std::string>();
	std::cout << "\x1b[31m"; // red
	std::cout << jresponse[0]["program_error"].template get<std::string>();
	std::cout << "\x1b[0m"; // clear
	std::cout << jresponse[0]["program_output"].template get<std::string>();
}
