/* Send code to be compiled and run by wandbox. */
//TODO: Add option to turn colorized output on/off
//TODO: Add option to strip ANSI color codes from the response

#include "nlohmann/json.hpp"
#include <curl/curl.h>
#include <iostream>
#include <string>
#include <iterator>
#include <fstream>
#include <vector>
#include <set>
#include <optional>

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

    std::string get_info()
    {
        std::string response;
        curl_initer ci;
        auto curl{make_curl()};
        curl_easy_setopt(curl.get(), CURLOPT_URL,
                "https://wandbox.org/api/list.json");
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, curl_callback);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl.get(), CURLOPT_CA_CACHE_TIMEOUT, 604800L);
        curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "wandboxit/1.0");
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

    std::optional<std::string> get_option(
            std::vector<std::string>& args,
            const std::string& option_name)
    {
        std::string option{"--" + option_name + "="};
        auto found{
            std::find_if(args.begin(), args.end(),
                    [&option](auto arg){ return starts_with(arg, option); })};
        if (args.end() == found) return std::nullopt;
        auto value{found->substr(option.size())};
        args.erase(found);
        return value;
    }

    bool get_flag(
            std::vector<std::string>& args,
            const std::string& flag_name)
    {
        std::string flag{"--"s + flag_name};
        auto found{std::find(args.begin(), args.end(), flag)};
        if (args.end() == found) return false;
        args.erase(found);
        return true;
    }

    auto get_languages(const nlohmann::json& info)
    {
        // jq '.[].language' list.json | sort -u
        std::set<std::string> languages;
        // We get an array inside an array?
        for (const auto& outer: info) {
            for (const auto& entry: outer) {
                languages.insert(entry["language"]);
            }
        }
        return languages;
    }

    auto get_compilers(const nlohmann::json& info, const std::string& language)
    {
        // jq '.[] | select(.language=="C++") | .name' list.json
        std::vector<std::string> compilers;
        for (const auto& outer: info) {
            for (const auto& entry: outer) {
                if (entry["language"] == language) {
                    compilers.push_back(entry["name"]);
                }
            }
        }
        return compilers;
    }

    auto get_standards(const nlohmann::json& info, const std::string& compiler)
    {
        //TODO: Surely there's a better way.
        // jq '.[] | select(.name=="gcc-head") | .switches[] | select(.name=="std-cxx") | .options[].name'
        std::vector<std::string> standards;
        for (const auto& outer: info) {
            for (const auto& inner: outer) {
                auto name{inner["name"].template get<std::string>()};
                if (name == compiler) {
                    for (const auto& switch_entry: inner["switches"]) {
                        auto name{
                            switch_entry["name"].template get<std::string>()};
                        if (("std-cxx" == name)
                                or ("std-c" == name))
                        {
                            for (const auto& option: switch_entry["options"]) {
                                auto name{option["name"].template
                                    get<std::string>()};
                                standards.push_back(name);
                            }
                        }
                    }
                }
            }
        }
        return standards;
    }

    void usage(std::string_view command)
    {
        std::cout
            << "usage: " << command
            << " [--compiler=compiler] [--std=standard] file\n"
            << "   or: " << command
            << " --list-languages\n"
            << "   or: " << command
            << " --list-compilers=language\n"
            << "   or: " << command
            << " --list-standards=compiler\n";
    }
}

int main(const int argc, const char** argv)
{
    // The fstream ctors still don't take string_views!?
    // So we'll parse the args into strings instead.
    std::vector<std::string> args(argv + 1, argv + argc);

    if (get_flag(args, "list-languages")) {
        auto info{nlohmann::json::parse(get_info())};
        auto list{get_languages(info)};
        std::copy(list.begin(), list.end(),
                std::ostream_iterator<std::string>(std::cout, "\n"));
        return EXIT_SUCCESS;
    } else if (auto lang{get_option(args, "list-compilers")}; lang) {
        auto info{nlohmann::json::parse(get_info())};
        auto list{get_compilers(info, *lang)};
        std::copy(list.begin(), list.end(),
                std::ostream_iterator<std::string>(std::cout, "\n"));
        return EXIT_SUCCESS;
    } else if (auto compiler{get_option(args, "list-standards")}; compiler) {
        auto info{nlohmann::json::parse(get_info())};
        auto list{get_standards(info, *compiler)};
        std::copy(list.begin(), list.end(),
                std::ostream_iterator<std::string>(std::cout, "\n"));
         return EXIT_SUCCESS;
    }

    auto compiler = get_option(args, "compiler");
    auto standard = get_option(args, "std");
    if (not standard) standard = get_option(args, "standard");

    if (args.size() != 1) {
        std::cerr << "Give me the name of a single file to compile!\n";
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    auto filename{args[0]};

    if (ends_with(filename, ".c")) {
        if (not compiler) compiler = "gcc-head-c";
        if (not standard) standard = "c11";
    } else {
        if (not compiler) compiler = "gcc-head";
        if (not standard) standard = "c++17";
    }

    std::ifstream file(filename);
    std::string code(std::istreambuf_iterator<char>{file}, {});
    nlohmann::json jrequest = {
        { "options", "warning,"s + *standard },
        { "compiler", *compiler },
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
