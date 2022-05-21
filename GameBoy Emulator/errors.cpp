#include "errors.h"

#include <Windows.h>

void fatal(int error_code, std::string func_name, std::string info) {
#ifndef _DEBUG
	ShowWindow(GetConsoleWindow(), SW_SHOW);
#endif
	std::cout << "\nFatal error in function " << func_name << "() : " << fatal_errors[error_code] << std::endl;
	if (info != "") std::cout << "More info: " << info << std::endl;
	std::cout << "Press any key to close the program.." << std::endl;
	std::cin.get();
	exit(EXIT_FAILURE);
}