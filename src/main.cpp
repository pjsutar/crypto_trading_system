#include "logger.hpp"

int main() {
    const char          c  = 'd';
    const int           i  = 3;
    const unsigned long ul = 65;
    const float         f  = 3.4f;       // f suffix — float literal, not double
    const double        d  = 34.56;
    const char*         s  = "test C-string";
    const std::string   ss = "test string";

    cts::Common::Logger logger("logging_example.log");

    logger.log("Logging a char:% an int:% and an unsigned:%\n", c, i, ul);
    logger.log("Logging a float:% and a double:%\n", f, d);
    logger.log("Logging a C-string:'%'\n", s);
    logger.log("Logging a string:'%'\n", ss);

    return 0;
}