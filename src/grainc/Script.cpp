#include "Script.hpp"
#include <fstream>
#include "Logger.hpp"

using namespace std;

bool Script::read(const std::string& filename, ILogStream* logStream)
{
	ifstream input(filename.c_str());
	if(!input.good())
	{
		Logger(logStream) << "Can't open file '" << filename << "'";
		return false;
	}

	mBody.clear();
	mAttributes.clear();
	mParams.clear();
	mDependencies.clear();
	mCustomDeclarations.clear();

	string line;
	string token;
	stringstream ss;
	vector<string> tokens;
	unsigned int lineCount = 0;
	unsigned int firstBodyLine = 0;

	while(getline(input, line))
	{
		++lineCount;

		tokens.clear();
		ss.str(string());
		ss.clear();
		ss << line;
		while(getline(ss, token, ' '))
		{
			tokens.push_back(token);
		}

		string& firstTok = tokens[0];
		if(tokens.empty())
		{
			mBody += '\n';
			continue;
		}

		if(firstTok == "@param")
		{
			DataType::Enum paramType;
			if(DataType::parse(tokens[1], paramType))
			{
				//TODO: handle duplicates
				mParams.insert(make_pair(tokens[2], paramType));
			}
			else
			{
				Logger(logStream)
					<< filename << "(" << lineCount << "): " << "Invalid type '" << tokens[1] << "'";
				return false;
			}
		}
		else if(firstTok == "@attribute")
		{
			DataType::Enum paramType;
			if(DataType::parse(tokens[1], paramType))
			{
				//TODO: handle duplicates
				mAttributes.insert(make_pair(tokens[2], paramType));
			}
			else
			{
				Logger(logStream)
					<< filename << "(" << lineCount << "): " << "Invalid type '" << tokens[1] << "'";
				return false;
			}
		}
		else if(firstTok == "@require")
		{
			mDependencies.push_back(tokens[1]);
		}
		else if(firstTok == "@declare")
		{
			//TODO: #line
			mCustomDeclarations += line.substr(9, line.length() - 9);
			mCustomDeclarations += '\n';
		}
		else
		{
			if(mFirstBodyLine == 0)
			{
				mFirstBodyLine = lineCount;
			}

			mBody += line;
			mBody += "\n";
		}
	}

	mFilename = filename;
	return true;
}

bool Script::parseFilename(
	const std::string& filename,
	std::string& scriptName,
	ScriptType::Enum& scriptType
)
{
	const string::size_type dotPos = filename.find_last_of('.');

	if(dotPos == string::npos) { return false; }

	const string::size_type slashPos = filename.find_last_of('/');
	const string::size_type scriptNameStart = slashPos == string::npos ? 0 : slashPos + 1;
	scriptName = filename.substr(scriptNameStart, dotPos - scriptNameStart);
	const string extension = filename.substr(dotPos + 1, filename.size() - slashPos);

	return ScriptType::parse(extension, scriptType);
}
