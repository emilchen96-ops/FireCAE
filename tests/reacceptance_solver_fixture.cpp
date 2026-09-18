// Controlled process fixture for boundary regression tests. This is NOT FDS.
// Build in its own directory with OUTPUT_NAME "fds" so FdsRunner can launch it.
#include <chrono>
#include <cstdio>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <string>
#include <thread>

int main(int argc, char** argv)
{
    if (argc < 2) return 2;
    std::ifstream input(argv[argc - 1]);
    const std::string text((std::istreambuf_iterator<char>(input)), {});
    if (text.empty()) return 3;
    if (text.find("TEST_ZERO_NO_OUTPUT") != std::string::npos) return 0;
    std::smatch match;
    std::string chid = "case";
    if (std::regex_search(text, match, std::regex("CHID[ ]*=[ ]*'([^']+)'")))
        chid = match[1].str();
    if (text.find("TEST_STREAM_ENCODING") != std::string::npos) {
#ifdef _WIN32
        _setmode(_fileno(stdout), _O_BINARY);
        _setmode(_fileno(stderr), _O_BINARY);
#endif
        const std::string title = "\xe6\xa1\x8c\xe9\x9d\xa2\xe9\xaa\x8c\xe6\x94\xb6 Save First Fire";
        const std::string output = "UTF8: " + title + "\r\nASCII: 30.0\r\nUTF8 tail: " + title;
        const std::string error = "LOCAL: \xb4\xed\xce\xf3\r\nlocal tail: \xb4\xed\xce\xf3";
        std::ofstream(chid + ".smv") << "TITLE\n Stream regression fixture only\n";
        std::ofstream(chid + ".out") << "Total Time: 1.0 s\nSTOP: FDS completed successfully\n";
        for (std::size_t i = 0; i < output.size() || i < error.size(); ++i) {
            if (i < output.size()) { std::cout.put(output[i]); std::cout.flush(); }
            if (i < error.size()) { std::cerr.put(error[i]); std::cerr.flush(); }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        std::ofstream(chid + ".ready") << "Both channels flushed; EOF tails have no newline.\n";
        if (text.find("TEST_STREAM_WAIT") != std::string::npos)
            std::this_thread::sleep_for(std::chrono::seconds(10));
        return 0;
    }
    if (text.find("TEST_CATF") != std::string::npos) {
        if (!std::regex_search(text, match, std::regex("OTHER_FILES[ ]*=[ ]*'([^']+)'")) ||
            !std::filesystem::is_regular_file(match[1].str())) return 5;
        chid += "_cat";
    }
    if (text.find("TEST_RESTART") != std::string::npos) {
        std::string sourceChid = chid;
        if (std::regex_search(text, match, std::regex("RESTART_CHID[ ]*=[ ]*'([^']+)'"))) sourceChid = match[1].str();
        std::ifstream checkpoint(sourceChid + "_1.restart");
        double baseline = -1;
        if (!(checkpoint >> baseline)) return 5;
        const bool append = sourceChid == chid;
        if (append && !std::filesystem::is_regular_file(chid + ".smv")) return 6;
        if (!append) std::ofstream(chid + ".smv") << "TITLE\n Restart fixture\n";
        std::ofstream log(chid + ".out", append ? std::ios::app : std::ios::trunc);
        log << "SOURCE_BASELINE=" << baseline << '\n';
        if (text.find("TEST_RESTART_NO_ADVANCE") != std::string::npos) {
            log << "STOP: FDS completed successfully\n";
            return 0;
        }
        double end = baseline + 0.2;
        if (std::regex_search(text, match, std::regex("T_END[ ]*=[ ]*([0-9.]+)"))) end = std::stod(match[1].str());
        std::ofstream steps(chid + "_steps.csv", append ? std::ios::app : std::ios::trunc);
        if (!append) steps << ",,s,s,s\nTime Step,Wall Time,Step Size,Simulation Time,CPU Time\n";
        steps << "2,fixture," << end - baseline << ',' << end << ",0\n";
        std::ofstream(chid + "_1.restart") << end << '\n';
        std::ofstream(chid + "_hrr.csv") << "s,kW\nTime,HRR\n0,0\n" << end << ",0\n";
        log << "Total Time: " << end << " s\nSTOP: FDS completed successfully\n";
        std::cout << "STOP: FDS completed successfully\n";
        return 0;
    }
    std::string value = "10";
    if (std::regex_search(text, match, std::regex("TEST_VALUE=([0-9]+)")))
        value = match[1].str();
    if (text.find("TEST_DELAY") != std::string::npos)
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    std::ofstream(chid + ".smv") << "TITLE\n Regression fixture only\n";
    std::ofstream log(chid + ".out");
    log << "Revision : FDS-fixture-0\nTEST_VALUE=" << value << '\n';
    if (std::regex_search(text, match, std::regex("(?:TMPFILE|FILE)[ ]*=[ ]*'([^']+)'"))) {
        const std::string dependencyPath = match[1].str();
        std::ifstream dependency(dependencyPath);
        if (!dependency) {
            log << "ERROR: relative dependency could not be opened\n";
            return 4;
        }
        std::string dependencyValue;
        std::getline(dependency, dependencyValue);
        log << "DEPENDENCY=" << dependencyValue << '\n';
        if (text.find("TEST_CSVF_TWO_MESHES") != std::string::npos) {
            const std::string siblingPath = dependencyPath.substr(0, dependencyPath.size() - 5) + "2.csv";
            std::ifstream sibling(siblingPath);
            if (!sibling) {
                log << "ERROR(440): Missing second mesh CSVF input\n";
                return 0; // Real FDS 6.11.1 also exits zero on this boundary.
            }
            std::getline(sibling, dependencyValue);
            log << "SIBLING=" << dependencyValue << '\n';
        }
    }
    if (text.find("TEST_ZERO_ERROR") != std::string::npos) {
        log << "ERROR: controlled fixture failure\n";
        std::cerr << "ERROR: controlled fixture failure\n";
        return 0;
    }
    std::ofstream(chid + "_hrr.csv")
        << "s,kW\nTime,HRR\n0,0\n1," << value << '\n';
    log << "Total Time: 1.0 s\nSTOP: FDS completed successfully\n";
    std::cout << "Simulation Time: 1.0\nSTOP: FDS completed successfully\n";
    return 0;
}
