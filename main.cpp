#include "protocol/executor.hpp"
#include "protocol/parser.hpp"
#include "store.hpp"

#include <iostream>
#include <string>

void run_command(
    const std::string& input,
    KeyValueStore& store
)
{
    auto command = parse_command(input);

    std::cout << "> " << input << '\n';

    if (!command) {
        std::cout << "INVALID_COMMAND\n";
        return;
    }

    std::cout << execute_command(*command, store)
              << '\n';
}

int main()
{
    KeyValueStore store;

    auto command = parse_command("SET name James");

    if (command) {
      std::cout << execute_command(*command, store);
    }

    auto command_get = parse_command("GET name");

    if (command) {
      std::cout << execute_command(*command_get, store);
    }
}
