#include <cstdlib>
#include <iostream>
#include <string>

#include "app.h"

int main()
{
	const std::string actual = greeting_message();
	const std::string expected = "Hello cqg_binance_service...";

	if (actual != expected)
	{
		std::cerr << "Expected: " << expected << '\n';
		std::cerr << "Actual: " << actual << '\n';
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}