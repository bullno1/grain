#ifndef GRAINC_COMPILER_HPP
#define GRAINC_COMPILER_HPP

#include <set>
#include <vector>
#include <string>

struct CompileOptions
{
	std::string mOutput;
	std::vector<std::string> mInputs;
	std::vector<std::string> mIncludePaths;
};

int compile(const CompileOptions& opts);

#endif
