/*
   Send code to be compiled and run by wandbox.
*/

#include "nlohmann/json.hpp"
#include <curl/curl.h>
#include <iostream>
#include <string>
#include <iterator>
#include <fstream>
#include <vector>

using namespace std::literals;

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

    struct curl_initer {
        explicit curl_initer() { curl_global_init(CURL_GLOBAL_DEFAULT); }
        ~curl_initer() { curl_global_cleanup(); }
    };

    using curl_ptr = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
    curl_ptr make_curl()
    {
        CURL* curl = curl_easy_init();
        if (not curl) {
            throw std::runtime_error{"curl_easy_init failed"};
        }
        return curl_ptr{curl, &curl_easy_cleanup};
    }

    struct slist
    {
        curl_slist* list{nullptr};
        explicit slist(){}
        ~slist() { if (list) curl_slist_free_all(list); }
        slist(const slist&) = delete;
        slist(slist&&) = delete;
        void append(const std::string& s)
        {
            curl_slist* temp{curl_slist_append(list, s.c_str())};
            if (not temp) {
                throw std::runtime_error{"curl_slist_append("s + s + ")"};
            } else {
                list = temp;
            }
        }
    };

    std::string post(std::string_view request)
    {
        std::string response;
        curl_initer ci;
        auto curl{make_curl()};
        curl_easy_setopt(curl.get(), CURLOPT_URL,
                "https://wandbox.org/api/compile.json");
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, curl_callback);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl.get(), CURLOPT_CA_CACHE_TIMEOUT, 604800L);
        curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "wandboxit/1.0");
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, request.data());
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, request.size());
        slist headers;
        headers.append("Content-type: application/json");
        curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.list);
        CURLcode rc = curl_easy_perform(curl.get());
        if (CURLE_OK != rc) {
            throw std::runtime_error{
                "curl_easy_perform: "s + curl_easy_strerror(rc)};
        }
        return response;
    }

    bool starts_with(const std::string& s, std::string_view prefix)
    {
        return 0 == s.rfind(prefix, 0);
    }

    bool ends_with(const std::string& s, std::string_view suffix)
    {
        return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin());
    }
}

std::string get_option(std::vector<std::string>& args, const std::string& option_name, const std::string& default_value)
{
    //TODO: Handle if the same option is given twice.
    std::string option{"--" + option_name + "="};
    auto found{
        std::find_if(args.begin(), args.end(),
                [&option](auto arg){ return starts_with(arg, option); })};
    if (args.end() == found) return default_value;
    auto value{found->substr(option.size())};
    args.erase(found);
    return value;
}

int main(const int argc, const char** argv)
{
    //TODO: An options to get language, compiler, and standard options.

    // The fstream ctors still don't take string_views!?
    // So we'll parse the args into strings instead.
    std::vector<std::string> args(argv + 1, argv + argc);

    std::string default_compiler{"gcc-head"};
    std::string default_standard{"c++2b"};
    //TODO: This could be done better...
    auto found{
        std::find_if(args.begin(), args.end(),
                [](auto arg){ return not starts_with(arg, "--"); })};
    if (args.end() != found) {
        if (ends_with(*found, ".cpp")) {
            default_compiler = "gcc-head";
            default_standard = "c++2b";
        } else if (ends_with(*found, ".c")) {
            default_compiler = "gcc-head-c";
            default_standard = "c11";
        }
    }

    auto compiler = get_option(args, "compiler", "gcc-head");
    auto standard = get_option(args, "std", "c++2b");

    //TODO: Use a C standard option if using a C compiler.

    if (args.empty()) {
        std::cerr << "Give me the name of a file to compile!\n";
        return EXIT_FAILURE;
    }
    std::ifstream file(args[0]);
    std::string code(std::istreambuf_iterator<char>{file}, {});
    nlohmann::json jrequest = {
        { "options", "warning,"s + standard },
        { "compiler", compiler },
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
