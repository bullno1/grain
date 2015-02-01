#ifndef GRAINC_SOURCE_MAP_HPP
#define GRAINC_SOURCE_MAP_HPP

#include <string>
#include <vector>

class SourceMap
{
public:
	SourceMap();

	unsigned int addMapping(
		const std::string& filename,
		unsigned int startLine,
		unsigned int numLines
	);
	void lookup(
		unsigned int line,
		std::string& filename,
		unsigned int& originalLine) const;

private:
	struct Mapping
	{
		std::string mFilename;
		unsigned int mStartLine;
		unsigned int mNumLines;
	};
	typedef std::vector<Mapping> Mappings;

	Mappings mMappings;
	unsigned int mNumLines;
};

#endif
