#include <iostream>
#include <string>

#include "protocol/parser.hpp"

void test_parser(const std::string& input)
{
    std::cout << "Input: \"" << input << "\"\n";

    auto command = parse_command(input);

    if (!command) {
        std::cout << "Result: INVALID\n\n";
        return;
    }

    std::cout << "Result: VALID\n";
    std::cout << "Key:   \"" << command->key << "\"\n";
    std::cout << "Value: \"" << command->value << "\"\n\n";
}

int main()
{
    // Valid commands
    test_parser("SET name James");
    test_parser("GET name");
    test_parser("DELETE name");
    test_parser("EXISTS name");
    test_parser("COUNT");

    // Invalid commands
    test_parser("");
    test_parser("SET");
    test_parser("SET name");
    test_parser("GET");
    test_parser("GET name extra");
    test_parser("DELETE");
    test_parser("EXISTS");
    test_parser("COUNT extra");
    test_parser("BANANA name");

    return 0;
}
