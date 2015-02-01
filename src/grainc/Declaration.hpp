#ifndef GRAINC_DECLARATION_HPP
#define GRAINC_DECLARATION_HPP

#include "DataType.hpp"
#include <map>
#include <string>

namespace DeclarationType
{
	enum Enum
	{
		Param,
		Attribute
	};
};

struct Declaration
{
	DeclarationType::Enum mDeclType;
	DataType::Enum mDataType;
	unsigned int mLine;
};

typedef std::map<std::string, Declaration> Declarations;

class DeclarationHelper
{
public:
	DeclarationHelper(Declarations& Declarations);
	void copy(const DeclarationHelper& other);

	bool declare(
		const std::string& name,
		DeclarationType::Enum declType,
		DataType::Enum dataType,
		unsigned int line,
		const std::string& filename,
		bool allowCompatibleDup,
		const std::string** conflictedFile = 0,
		const Declaration** conflictedDecl = 0
	);

	bool declare(
		const std::string& name,
		const Declaration& decl,
		const std::string& filename,
		bool allowCompatibleDup,
		const std::string** conflictedFile = 0,
		const Declaration** conflictedDecl = 0
	);
private:
	Declarations& mDeclarations;
	std::map<std::string, std::string> mDeclToFilename;
};

#endif
