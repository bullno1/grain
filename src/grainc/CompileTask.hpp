#ifndef GRAINC_COMPILE_TASK_HPP
#define GRAINC_COMPILE_TASK_HPP

#include <vector>

struct CompileTask
{
	bool mOptimize;
	const char* mOutput;
	std::vector<const char*> mInputs;
	std::vector<const char*> mIncludePaths;
};

#endif
