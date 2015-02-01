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

	mFilename = filename;
	mBody.clear();
	mDeclarations.clear();
	mDependencies.clear();
	mCustomDeclarations.clear();
	mNumBodyLines = 0;
	DeclarationHelper declHelper(mDeclarations);

	string line;
	string token;
	stringstream ss;
	vector<string> tokens;
	unsigned int lineCount = 0;

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

		if(tokens.empty()) { continue; }

		string& firstTok = tokens[0];
		if(firstTok == "@param" || firstTok == "@attribute")
		{
			DeclarationType::Enum declType = firstTok == "@param" ? DeclarationType::Param : DeclarationType::Attribute;
			const string& declName = tokens[2];
			const string& typeName = tokens[1];

			DataType::Enum dataType;
			if(!DataType::parse(typeName, dataType))
			{
				Logger(logStream)
					<< filename
					<< ':' << line
					<< "Invalid type '" << typeName << "'";
				return false;
			}

			const Declaration* conflictedDecl;
			if(!declHelper.declare(
					declName,
					declType,
					dataType,
					lineCount,
					filename,
					false,
					NULL,
					&conflictedDecl
				))
			{
				Logger(logStream)
					<< filename
					<< ':' << lineCount
					<< ':' << "Redeclaration of '" << declName << "'"
					<< " (previously found at line " << conflictedDecl->mLine << ')';
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
		else // reached body
		{
			mFirstBodyLine = lineCount;
			mBody += line;
			mBody += "\n";
			++mNumBodyLines;
			break;
		}
	}

	if(mNumBodyLines != 0) // reached body before EOF
	{
		while(getline(input, line))
		{
			mBody += line;
			mBody += "\n";
			++mNumBodyLines;
		}
	}
	else
	{
		mFirstBodyLine = 1;
		mNumBodyLines = 0;
	}

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
