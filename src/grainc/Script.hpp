#ifndef GRAINC_SCRIPT_HPP
#define GRAINC_SCRIPT_HPP

#include <string>
#include <map>
#include <vector>
#include "DataType.hpp"
#include "ScriptType.hpp"

class ILogStream;

struct Script
{
	typedef std::map<std::string, DataType::Enum> Declarations;

	ScriptType::Enum mType;
	std::string mName;
	std::string mFilename;
	std::string mCustomDeclarations;
	Declarations mParams;
	Declarations mAttributes;
	std::string mBody;
	std::vector<std::string> mDependencies;
	unsigned int mFirstBodyLine;
	unsigned int mNumBodyLines;
	unsigned int mGeneratedCodeStartLine;
	std::string mGeneratedCode;

	bool read(const std::string& filename, ILogStream* logStream);

	static bool parseFilename(
		const std::string& filename,
		std::string& scriptName,
		ScriptType::Enum& scriptType
	);
};

#endif
