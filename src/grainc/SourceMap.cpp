#include "SourceMap.hpp"

SourceMap::SourceMap()
	:mNumLines(0)
{}

unsigned int SourceMap::addMapping(
	const std::string& filename,
	unsigned int startLine,
	unsigned int numLines
)
{
	Mapping newMapping;
	newMapping.mFilename = filename;
	newMapping.mStartLine = startLine;
	newMapping.mNumLines = numLines;
	mMappings.push_back(newMapping);

	unsigned int targetLine = mNumLines;
	mNumLines += numLines;
	return targetLine;
}

void SourceMap::lookup(unsigned int line, std::string& filename, unsigned int& originalLine) const
{
	const Mapping* currentMapping = NULL;
	for(Mappings::const_iterator itr = mMappings.begin(); itr != mMappings.end(); ++itr)
	{
		currentMapping = &(*itr);
		if(line <= currentMapping->mNumLines)
		{
			filename = currentMapping->mFilename;
			originalLine = line + currentMapping->mStartLine - 1;
			return;
		}
		else
		{
			line -= currentMapping->mNumLines;
		}
	}

	if(currentMapping != NULL)
	{
		filename = currentMapping->mFilename;
		originalLine = line + currentMapping->mStartLine - 1;
	}
}
