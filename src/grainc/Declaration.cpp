#include "Declaration.hpp"

DeclarationHelper::DeclarationHelper(Declarations& declarations)
	:mDeclarations(declarations)
{}

bool DeclarationHelper::declare(
	const std::string& name,
	DeclarationType::Enum declType,
	DataType::Enum dataType,
	unsigned int line,
	const std::string& filename,
	bool allowCompatibleDup,
	const std::string** conflictedFile,
	const Declaration** conflictedDecl
) {
	Declarations::const_iterator itr = mDeclarations.find(name);
	if(itr != mDeclarations.end())
	{
		const Declaration& oldDecl = itr->second;
		bool compatible = (oldDecl.mDeclType == declType) && (oldDecl.mDataType == dataType);
		if(allowCompatibleDup && compatible)
		{
			return true;
		}
		else
		{
			if(conflictedFile)
			{
				(*conflictedFile) = &mDeclToFilename[name];
			}

			if(conflictedDecl)
			{
				(*conflictedDecl) = &(itr->second);
			}

			return false;
		}
	}
	else
	{
		Declaration newDecl;
		newDecl.mLine = line;
		newDecl.mDataType = dataType;
		newDecl.mDeclType = declType;
		mDeclarations.insert(std::make_pair(name, newDecl));
		mDeclToFilename.insert(std::make_pair(name, filename));
		return true;
	}
}

bool DeclarationHelper::declare(
	const std::string& name,
	const Declaration& decl,
	const std::string& filename,
	bool allowCompatibleDup,
	const std::string** conflictedFile,
	const Declaration** conflictedDecl
)
{
	return declare(
		name,
		decl.mDeclType,
		decl.mDataType,
		decl.mLine,
		filename,
		allowCompatibleDup,
		conflictedFile,
		conflictedDecl
	);
}

void DeclarationHelper::copy(const DeclarationHelper& other)
{
	if(&other != this)
	{
		mDeclarations = other.mDeclarations;
		mDeclToFilename = other.mDeclToFilename;
	}
}
